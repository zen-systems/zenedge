#!/usr/bin/env python3
"""
ZENEDGE Gym Agent - Connects OpenAI Gym to ZENEDGE Kernel
Serves as a "Body" (Peripheral) for the Kernel "Brain".
"""

import sys
import gym
import time
import struct
import argparse
from pathlib import Path

import traceback
import numpy as np
# Monkeypatch for older gym versions with newer numpy
if not hasattr(np, 'bool8'):
    np.bool8 = np.bool_

# Fix path to allow importing zenedge_bridge
sys.path.append(str(Path(__file__).parent.parent))

from bridge.zenedge_bridge import ZenedgeBridge
from bridge.protocol import (
    RSP_OK,
    RSP_ERROR,
    DTYPE_FLOAT32,
    CMD_ENV_RESET,
    CMD_ENV_STEP,
    CMD_IFR_PERSIST,
    CMD_ARB_EPISODE,
    CMD_TELEMETRY_POLL,
    BLOB_TYPE_TENSOR,
    ENV_RESET_FLAG_STREAM,
    env_step_unpack,
)
from bridge.handlers import register_all_handlers # Optional base handlers
from bridge.stream import StreamRings
from bridge.ifr import parse_ifr_blob
from bridge.arbiter import query_next_profile, verify_ifr_archive

OBS_STRUCT_FMT = "4ffff"  # 7 floats: obs[4], reward, done, model_id
OBS_POOL_SIZE = 8

class GymHandler:
    def __init__(self, bridge, env_name="CartPole-v1"):
        self.env = gym.make(env_name)
        self.env_name = env_name
        self.obs = None
        self.bridge = bridge
        self.model_blob_id = 0
        self.baseline_model_id = 0
        self.obs_pool_ids = []
        self.free_obs_ids = []
        self.in_flight = set()
        self.stream = StreamRings(bridge.shm)
        self.streaming = False
        print(f"[GYM] Initialized environment: {env_name}")
        # Model upload deferred to first reset to allow heap init

    def _upload_model(self):
        """Upload a linear policy to the shared heap."""
        if self.model_blob_id != 0:
            return # Already uploaded

        # Simple Linear Policy for CartPole
        # Weights found by simple evolution or heuristic equivalent
        # Heuristic: 1 if (angle * 1.0 + ang_vel * 0.5 + pos * 0.0 + vel * 0.5) > 0
        # Weights: [0.0, 0.5, 1.0, 0.5]
        weights = np.array([0.0, 0.5, 1.0, 0.5], dtype=np.float32)
        
        self._set_model_weights(weights)

    def _set_model_weights(self, weights: np.ndarray) -> int:
        """Upload weights into a fresh blob and swap the active model."""
        blob_id = self.bridge.heap.allocate_blob(weights.nbytes, blob_type=BLOB_TYPE_TENSOR)
        if not blob_id:
            print("[GYM] Error: Failed to upload model (heap full or not init?)")
            return 0

        self.bridge.heap.write_blob_data(blob_id, weights.tobytes())
        if self.model_blob_id:
            self.bridge.heap.free_blob(self.model_blob_id)
        self.model_blob_id = blob_id
        if not self.baseline_model_id:
            self.baseline_model_id = blob_id
        print(f"[GYM] Uploaded Model to Blob {self.model_blob_id}")
        return blob_id

    def _init_obs_pool(self):
        """Allocate a fixed pool of obs blobs to avoid per-step allocations."""
        if self.obs_pool_ids:
            self.free_obs_ids = self.obs_pool_ids.copy()
            self.in_flight.clear()
            return

        obs_size = struct.calcsize(OBS_STRUCT_FMT)
        for _ in range(OBS_POOL_SIZE):
            blob_id = self.bridge.heap.allocate_blob(obs_size, blob_type=BLOB_TYPE_TENSOR)
            if not blob_id:
                print("[GYM] Error: Failed to allocate obs pool blob")
                break
            self.obs_pool_ids.append(blob_id)

        self.free_obs_ids = self.obs_pool_ids.copy()
        self.in_flight.clear()

        if not self.obs_pool_ids:
            print("[GYM] Error: Obs pool allocation failed, falling back to per-step allocs")

    def _release_obs_blob(self, blob_id):
        if blob_id and blob_id in self.in_flight:
            self.in_flight.remove(blob_id)
            self.free_obs_ids.append(blob_id)

    def _claim_obs_blob(self):
        if self.free_obs_ids:
            blob_id = self.free_obs_ids.pop(0)
            self.in_flight.add(blob_id)
            return blob_id
        if self.obs_pool_ids:
            blob_id = self.obs_pool_ids[0]
            print(f"[GYM] Warn: Obs pool exhausted, reusing blob {blob_id}")
            return blob_id
        return 0

    def pack_step_data(self, obs, reward=0.0, done=0.0):
        """
        Pack step data into a blob.
        Layout (7 floats / 28 bytes):
        [0-3] Obs
        [4]   Reward
        [5]   Done
        [6]   Model Blob ID (as float)
        """
        # Cast Model ID to float (hacky but works for small IDs in strict 32-bit float)
        # 16M integer precision in float32 is enough for blob_id
        model_id_f = float(self.model_blob_id)
        
        data = struct.pack(OBS_STRUCT_FMT, *obs, float(reward), float(done), model_id_f)

        blob_id = self._claim_obs_blob()
        if not blob_id and not self.obs_pool_ids:
            blob_id = self.bridge.heap.allocate_blob(len(data), blob_type=BLOB_TYPE_TENSOR)

        if blob_id:
            self.bridge.heap.write_blob_data(blob_id, data)
            return blob_id
        return 0

    def handle_reset(self, bridge, packet):
        print(f"[GYM] Resetting environment...")
        self._upload_model()
        self.streaming = self.stream.ready() and (packet.payload_id & ENV_RESET_FLAG_STREAM)
        if not self.streaming:
            self._init_obs_pool()
            self.free_obs_ids = self.obs_pool_ids.copy()
            self.in_flight.clear()
        self.obs, info = self.env.reset()
        if self.streaming:
            seq = 0
            obs_entry = (
                seq,
                float(self.obs[0]), float(self.obs[1]),
                float(self.obs[2]), float(self.obs[3]),
                0.0, 0.0, float(self.model_blob_id),
            )
            while not self.stream.obs_ring.push(obs_entry):
                time.sleep(0.0005)
            return RSP_OK, 0
        else:
            blob_id = self.pack_step_data(self.obs)
            return RSP_OK, blob_id

    def handle_step(self, bridge, packet):
        if self.streaming:
            print("[GYM] Warning: CMD_ENV_STEP received while streaming (ignored)")
            return RSP_ERROR, 0
        action, ack_blob_id = env_step_unpack(int(packet.payload_id))
        self._release_obs_blob(ack_blob_id)
        try:
            self.obs, reward, terminated, truncated, info = self.env.step(action)
            done = 1.0 if (terminated or truncated) else 0.0
            blob_id = self.pack_step_data(self.obs, reward, done)
            if blob_id:
                return RSP_OK, blob_id
            return RSP_ERROR, 0
        except Exception as e:
            print(f"[GYM] Step Error: {e}")
            traceback.print_exc()
            return RSP_ERROR, 0

    def handle_arb_episode(self, bridge, packet):
        if packet.payload_id == 0:
            print("[GYM] ARB_EPISODE: missing payload")
            return RSP_ERROR, 0

        data = bridge.heap.read_blob_data(packet.payload_id)
        rec = parse_ifr_blob(data)
        if not rec or not rec.get("hash_ok"):
            print("[GYM] ARB_EPISODE: invalid IFR")
            return RSP_ERROR, 0

        decision = query_next_profile(data, rec)
        decision_code = int(decision.get("decision_code", 0)) if isinstance(decision, dict) else 0
        recommended_model_id = int(decision.get("recommended_model_id", self.model_blob_id or self.baseline_model_id or 0)) \
            if isinstance(decision, dict) else (self.model_blob_id or 0)
        reason = decision.get("reason") if isinstance(decision, dict) else ""

        if decision_code == 1:  # PROMOTE
            if self.model_blob_id:
                self.baseline_model_id = self.model_blob_id
                recommended_model_id = self.model_blob_id
        elif decision_code in (2, 3):  # REJECT or SAFE_MODE
            if self.baseline_model_id:
                self.model_blob_id = self.baseline_model_id
                recommended_model_id = self.baseline_model_id

        packed = ((decision_code & 0xFFFF) << 16) | (recommended_model_id & 0xFFFF)
        print(f"[GYM] ARB_EPISODE decision={decision_code} model={recommended_model_id} reason={reason}")
        return RSP_OK, packed
    def process_stream_step(self) -> bool:
        if not self.streaming:
            return False

        entry = self.stream.act_ring.pop()
        if entry is None:
            return False

        seq, action, _flags, _ack_seq, _reserved = entry
        try:
            self.obs, reward, terminated, truncated, info = self.env.step(int(action))
            done = 1.0 if (terminated or truncated) else 0.0
            obs_seq = seq + 1
            obs_entry = (
                obs_seq,
                float(self.obs[0]), float(self.obs[1]),
                float(self.obs[2]), float(self.obs[3]),
                float(reward), float(done), float(self.model_blob_id),
            )
            while not self.stream.obs_ring.push(obs_entry):
                time.sleep(0.0005)
            return True
        except Exception as e:
            print(f"[GYM] Stream Step Error: {e}")
            traceback.print_exc()
            return False

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", default="CartPole-v1")
    parser.add_argument("--shm", default="/dev/shm/zenedge.shm")
    args = parser.parse_args()

    try:
        bridge = ZenedgeBridge(shm_path=args.shm, create=True)
    except Exception as e:
        print(f"Failed to load bridge: {e}")
        return

    gym_handler = GymHandler(bridge, args.env)
    verify_ifr_archive()

    bridge.register_handler(CMD_ENV_RESET, gym_handler.handle_reset)
    bridge.register_handler(CMD_ENV_STEP, gym_handler.handle_step)
    bridge.register_handler(CMD_ARB_EPISODE, gym_handler.handle_arb_episode)
    
    from bridge.handlers import handle_ping, handle_print, handle_ifr_persist, handle_telemetry_poll
    bridge.register_handler(0x0001, handle_ping)
    bridge.register_handler(0x0002, handle_print)
    bridge.register_handler(CMD_IFR_PERSIST, handle_ifr_persist)
    bridge.register_handler(CMD_TELEMETRY_POLL, handle_telemetry_poll)

    print("[GYM] Bridge running. Waiting for Kernel commands...")
    try:
        while True:
            did_cmd = bridge.run_once()
            did_stream = gym_handler.process_stream_step()
            if not did_cmd and not did_stream:
                time.sleep(0.0005)
    except KeyboardInterrupt:
        print("\n[GYM] Interrupted by user")

if __name__ == "__main__":
    main()
