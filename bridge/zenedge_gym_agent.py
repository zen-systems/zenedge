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
from bridge.protocol import RSP_OK, RSP_ERROR, DTYPE_FLOAT32, CMD_ENV_RESET, CMD_ENV_STEP
from bridge.handlers import register_all_handlers # Optional base handlers

class GymHandler:
    def __init__(self, bridge, env_name="CartPole-v1"):
        self.env = gym.make(env_name)
        self.env_name = env_name
        self.obs = None
        self.bridge = bridge
        self.model_blob_id = 0
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
        
        # Try to allocate. If heap not ready, fails.
        # But handle_reset is called by Kernel, so heap MUST be ready.
        self.model_blob_id = self.bridge.heap.allocate_blob(weights.nbytes, blob_type=0x01)
        if self.model_blob_id:
            self.bridge.heap.write_blob_data(self.model_blob_id, weights.tobytes())
            print(f"[GYM] Uploaded Model (Linear Policy) to Blob {self.model_blob_id}")
        else:
            print("[GYM] Error: Failed to upload model (heap full or not init?)")

    def pack_step_data(self, obs, reward=0.0, done=0.0):
        """
        Pack step data into a blob.
        Layout (8 floats / 32 bytes):
        [0-3] Obs
        [4]   Reward
        [5]   Done
        [6]   Model Blob ID (as float)
        [7]   Reserved
        """
        # Cast Model ID to float (hacky but works for small IDs in strict 32-bit float)
        # 16M integer precision in float32 is enough for blob_id
        model_id_f = float(self.model_blob_id)
        
        data = struct.pack('4ffff', *obs, float(reward), float(done), model_id_f)
        
        blob_id = self.bridge.heap.allocate_blob(len(data), blob_type=0x01)
        if blob_id:
             self.bridge.heap.write_blob_data(blob_id, data)
             return blob_id
        return 0

    def handle_reset(self, bridge, packet):
        print(f"[GYM] Resetting environment...")
        self._upload_model()
        self.obs, info = self.env.reset()
        blob_id = self.pack_step_data(self.obs)
        return RSP_OK, blob_id

    def handle_step(self, bridge, packet):
        action = int(packet.payload_id)
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

    bridge.register_handler(CMD_ENV_RESET, gym_handler.handle_reset)
    bridge.register_handler(CMD_ENV_STEP, gym_handler.handle_step)
    
    from bridge.handlers import handle_ping, handle_print
    bridge.register_handler(0x0001, handle_ping)
    bridge.register_handler(0x0002, handle_print)

    print("[GYM] Bridge running. Waiting for Kernel commands...")
    bridge.run()

if __name__ == "__main__":
    main()
