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
    RSP_OK,
    RSP_ERROR,
    RSP_BUSY,
    BLOB_TYPE_TENSOR,
    BLOB_TYPE_RESULT,
    Packet,
)

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


def handle_run_model(bridge: 'ZenedgeBridge', packet: Packet) -> Tuple[int, int]:
    """
    Handle CMD_RUN_MODEL - run PyTorch inference on tensor.

    The payload_id should reference a BLOB_TYPE_TENSOR blob containing
    the input tensor. The result tensor is written to a new blob and
    its blob_id is returned.
    """
    import torch

    if packet.payload_id == 0:
        print("[HANDLER] RUN_MODEL: no input tensor")
        return RSP_ERROR, 0

    # Read input tensor
    input_tensor = bridge.heap.read_tensor(packet.payload_id)
    if input_tensor is None:
        print(f"[HANDLER] RUN_MODEL: tensor {packet.payload_id} not found")
        return RSP_ERROR, 0

    print(f"[HANDLER] RUN_MODEL: input shape={input_tensor.shape}, "
          f"dtype={input_tensor.dtype}")

    # Convert to PyTorch tensor
    try:
        torch_input = torch.from_numpy(input_tensor.copy())

        # Move to device
        device = bridge.model_cache.device
        torch_input = torch_input.to(device)

        # Get model (use "default" model for now)
        # TODO: Add model selection via flags or separate command
        model = bridge.model_cache.get_or_load("default")

        # Run inference
        with torch.no_grad():
            result = model(torch_input)

        # Convert back to numpy
        result_np = result.cpu().numpy()

        print(f"[HANDLER] RUN_MODEL: output shape={result_np.shape}, "
              f"dtype={result_np.dtype}")

        # Allocate result blob and write tensor
        result_id = bridge.heap.allocate_tensor(result_np)
        if result_id is None:
            print("[HANDLER] RUN_MODEL: failed to allocate result blob")
            return RSP_ERROR, 0

        print(f"[HANDLER] RUN_MODEL: result in blob {result_id}")
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
