"""
ZENEDGE Command Handlers

Handler functions for each IPC command type.
Each handler receives the bridge and packet, returns (status, result).
"""

from typing import Tuple, TYPE_CHECKING
import numpy as np

from .protocol import (
    CMD_PING,
    CMD_PRINT,
    CMD_RUN_MODEL,
    CMD_IFR_PERSIST,
    CMD_TELEMETRY_POLL,
    RSP_OK,
    RSP_ERROR,
    RSP_BUSY,
    BLOB_TYPE_TENSOR,
    BLOB_TYPE_RESULT,
    BLOB_TYPE_RAW,
    IFR_STRUCT,
    TELEMETRY_STRUCT,
    Packet,
)
from .ifr import parse_ifr_blob

import json
import os
import time

if TYPE_CHECKING:
    from .zenedge_bridge import ZenedgeBridge


def handle_ping(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle CMD_PING - simple echo/heartbeat.

    Returns RSP_OK with no result.
    """
    print("[HANDLER] PING -> PONG")
    return RSP_OK, 0


def handle_print(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle CMD_PRINT - print string from heap blob.

    The payload_id should reference a blob containing a UTF-8 string.
    """
    if packet.payload_id == 0:
        print("[HANDLER] PRINT: (no payload)")
        return RSP_OK, 0

    data = bridge.heap.read_blob_data(packet.payload_id)
    if data is None:
        print(f"[HANDLER] PRINT: blob {packet.payload_id} not found")
        return RSP_ERROR, 0

    # Decode as UTF-8, stopping at null terminator
    try:
        text = data.split(b'\x00')[0].decode('utf-8', errors='replace')
        print(f"[ZENEDGE] {text}")
    except Exception as e:
        print(f"[HANDLER] PRINT decode error: {e}")
        return RSP_ERROR, 0

    return RSP_OK, 0


def handle_ifr_persist(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle CMD_IFR_PERSIST - persist a kernel-generated IFR record.
    """
    if packet.payload_id == 0:
        print("[HANDLER] IFR_PERSIST: missing payload")
        return RSP_ERROR, 0

    data = bridge.heap.read_blob_data(packet.payload_id)
    if not data or len(data) < IFR_STRUCT.size:
        print("[HANDLER] IFR_PERSIST: invalid blob data")
        return RSP_ERROR, 0

    parsed = parse_ifr_blob(data)
    if not parsed:
        print("[HANDLER] IFR_PERSIST: invalid IFR record")
        return RSP_ERROR, 0

    hash_ok = parsed["hash_ok"]

    record = {
        "magic": hex(parsed["magic"]),
        "version": parsed["version"],
        "flags": parsed["flags"],
        "job_id": parsed["job_id"],
        "episode_id": parsed["episode_id"],
        "model_id": parsed["model_id"],
        "record_size": parsed["record_size"],
        "ts_usec": parsed["ts_usec"],
        "goodput": parsed["goodput"],
        "profile_len": parsed["profile_len"],
        "profile": parsed["profile"],
        "hash": parsed["hash"].hex(),
        "hash_ok": hash_ok,
    }

    out_dir = "/tmp/zenedge_ifr"
    os.makedirs(out_dir, exist_ok=True)
    stamp = int(time.time())
    base = f"{out_dir}/ifr-{parsed['job_id']}-{parsed['episode_id']}-{stamp}"
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, sort_keys=True)
    with open(base + ".bin", "wb") as f:
        f.write(data[:IFR_STRUCT.size])

    if not hash_ok:
        print("[HANDLER] IFR_PERSIST: hash mismatch")
        return RSP_ERROR, 0

    print(f"[HANDLER] IFR_PERSIST: wrote {base}.json")
    return RSP_OK, 0


def handle_telemetry_poll(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle CMD_TELEMETRY_POLL - return a telemetry snapshot blob.
    """
    ts_usec = int(time.time() * 1_000_000)
    gpu_temp = float(os.getenv("ZENEDGE_GPU_TEMP_C", "70.0"))
    rdma_qp_depth = float(os.getenv("ZENEDGE_RDMA_QP_DEPTH", "128.0"))
    numa_locality = float(os.getenv("ZENEDGE_NUMA_LOCALITY", "1.0"))

    data = TELEMETRY_STRUCT.pack(ts_usec, gpu_temp, rdma_qp_depth, numa_locality)
    blob_id = bridge.heap.allocate_blob(len(data), BLOB_TYPE_RAW)
    if not blob_id:
        return RSP_ERROR, 0

    bridge.heap.write_blob_data(blob_id, data)
    return RSP_OK, blob_id


def handle_run_model(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle CMD_RUN_MODEL - run ORT inference on tensor.

    The payload_id should reference a BLOB_TYPE_TENSOR blob containing
    the input tensor. The result tensor is written to a new blob and
    its blob_id is returned.
    """
    if packet.payload_id == 0:
        print("[HANDLER] RUN_MODEL: no input tensor")
        return RSP_ERROR, 0

    # Read input tensor (Note: HeapManager could return a zero-copy view if optimized that way)
    input_tensor = bridge.heap.read_tensor(packet.payload_id)
    if input_tensor is None:
        print(f"[HANDLER] RUN_MODEL: tensor {packet.payload_id} not found")
        return RSP_ERROR, 0

    # Ensure float32 (ORT standard)
    if input_tensor.dtype != np.float32:
        # print(f"[HANDLER] Promoting {input_tensor.dtype} to float32")
        input_tensor = input_tensor.astype(np.float32)

    # print(f"[HANDLER] RUN_MODEL: input shape={input_tensor.shape}")

    try:
        # Get ORT session (using "default" if no model ID/flag passed yet - protocol limitation?)
        # TODO: Protocol upgrade should pass Model Name ID. For now, we reuse packet.flags or similar?
        # Actually CMD_MODEL_LOAD sets context?
        # Let's default to "default" for M0 parity, or "linear" if shape matches 784
        
        model_name = "default"
        # Heuristic for demo until protocol allows passing model name in Run
        if input_tensor.shape == (1, 784):
            model_name = "linear"

        session = bridge.model_cache.get_or_load(model_name)

        # Get input name (assume single input for now)
        input_name = session.get_inputs()[0].name
        output_name = session.get_outputs()[0].name
        
        # Run inference
        result = session.run([output_name], {input_name: input_tensor})[0]

        # print(f"[HANDLER] RUN_MODEL: output shape={result.shape}")

        # Allocate result blob and write tensor
        result_id = bridge.heap.allocate_tensor(result)
        if result_id is None:
            print("[HANDLER] RUN_MODEL: failed to allocate result blob")
            return RSP_ERROR, 0

        # print(f"[HANDLER] RUN_MODEL: result in blob {result_id}")
        return RSP_OK, result_id

    except Exception as e:
        print(f"[HANDLER] RUN_MODEL error: {e}")
        import traceback
        traceback.print_exc()
        return RSP_ERROR, 0


def handle_tensor_alloc(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle tensor allocation request.

    The payload_id encodes the size in bytes to allocate.
    Returns the new blob_id in result.
    """
    size = packet.payload_id
    if size == 0:
        size = 1024  # Default size

    blob_id = bridge.heap.allocate_blob(size, BLOB_TYPE_TENSOR)
    if blob_id is None:
        return RSP_ERROR, 0

    print(f"[HANDLER] TENSOR_ALLOC: allocated blob {blob_id} ({size} bytes)")
    return RSP_OK, blob_id


def handle_tensor_free(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle tensor free request.

    The payload_id is the blob_id to free.
    """
    if packet.payload_id == 0:
        return RSP_ERROR, 0

    if bridge.heap.free_blob(packet.payload_id):
        print(f"[HANDLER] TENSOR_FREE: freed blob {packet.payload_id}")
        return RSP_OK, 0
    else:
        return RSP_ERROR, 0


def handle_heap_stats(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle heap statistics request.

    Prints heap stats and returns free blocks in result.
    """
    stats = bridge.heap.get_stats()
    print(f"[HANDLER] HEAP_STATS:")
    print(f"  Total: {stats['total_bytes']} bytes ({stats['total_blocks']} blocks)")
    print(f"  Free: {stats['free_bytes']} bytes ({stats['free_blocks']} blocks)")
    print(f"  Used: {stats['total_bytes'] - stats['free_bytes']} bytes")
    print(f"  Next blob_id: {stats['next_blob_id']}")

    return RSP_OK, stats['free_blocks']


def handle_model_load(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle model preload request.

    The payload_id references a blob containing the model name as a string.
    """
    if packet.payload_id == 0:
        # Load default model
        try:
            bridge.model_cache.get_or_load("default")
            print("[HANDLER] MODEL_LOAD: loaded default model")
            return RSP_OK, 0
        except Exception as e:
            print(f"[HANDLER] MODEL_LOAD error: {e}")
            return RSP_ERROR, 0

    # Read model name from blob
    data = bridge.heap.read_blob_data(packet.payload_id)
    if data is None:
        return RSP_ERROR, 0

    model_name = data.split(b'\x00')[0].decode('utf-8', errors='replace')

    try:
        bridge.model_cache.get_or_load(model_name)
        print(f"[HANDLER] MODEL_LOAD: loaded model '{model_name}'")
        return RSP_OK, 0
    except Exception as e:
        print(f"[HANDLER] MODEL_LOAD error: {e}")
        return RSP_ERROR, 0


# Extended command IDs (add to protocol.py if needed)
CMD_TENSOR_ALLOC = 0x0020
CMD_TENSOR_FREE  = 0x0021
CMD_HEAP_STATS   = 0x0022
CMD_MODEL_LOAD   = 0x0030


def register_all_handlers(bridge: 'ZenedgeBridge'):
    """Register all command handlers with the bridge."""
    # Core commands
    bridge.register_handler(CMD_PING, handle_ping)
    bridge.register_handler(CMD_PRINT, handle_print)
    bridge.register_handler(CMD_RUN_MODEL, handle_run_model)

    # Extended commands
    bridge.register_handler(CMD_TENSOR_ALLOC, handle_tensor_alloc)
    bridge.register_handler(CMD_TENSOR_FREE, handle_tensor_free)
    bridge.register_handler(CMD_HEAP_STATS, handle_heap_stats)
    bridge.register_handler(CMD_MODEL_LOAD, handle_model_load)

    print("[HANDLERS] Registered handlers:")
    print(f"  CMD_PING ({CMD_PING:#06x})")
    print(f"  CMD_PRINT ({CMD_PRINT:#06x})")
    print(f"  CMD_RUN_MODEL ({CMD_RUN_MODEL:#06x})")
    print(f"  CMD_TENSOR_ALLOC ({CMD_TENSOR_ALLOC:#06x})")
    print(f"  CMD_TENSOR_FREE ({CMD_TENSOR_FREE:#06x})")
    print(f"  CMD_HEAP_STATS ({CMD_HEAP_STATS:#06x})")
    print(f"  CMD_MODEL_LOAD ({CMD_MODEL_LOAD:#06x})")
