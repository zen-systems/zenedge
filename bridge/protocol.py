"""
ZENEDGE IPC Protocol Definitions

This module contains constants and struct formats that mirror the C definitions
in kernel/ipc/ipc_proto.h. All values must match exactly for interoperability.
"""

import struct
from dataclasses import dataclass
from typing import Tuple

# =============================================================================
# SHARED MEMORY LAYOUT (1MB total)
# =============================================================================
# 0x00000 - 0x07FFF: Command ring (32KB)    - ZENEDGE -> Linux
# 0x08000 - 0x0FFFF: Response ring (32KB)   - Linux -> ZENEDGE
# 0x10000 - 0x100FF: Doorbell control (256B) - Interrupt signaling
# 0x10100 - 0x10FFF: Heap control block (~4KB)
# 0x11000 - 0xFDFFF: Heap data region (~948KB for tensors/models)
# 0xFE000 - 0xFEFFF: OBS ring (streaming)
# 0xFF000 - 0xFFFFF: ACTION ring (streaming)

IPC_CMD_RING_OFFSET  = 0x00000
IPC_RSP_RING_OFFSET  = 0x08000
IPC_DOORBELL_OFFSET  = 0x10000
IPC_HEAP_CTL_OFFSET  = 0x10100
IPC_HEAP_DATA_OFFSET = 0x11000
IPC_HEAP_DATA_SIZE   = 0xED000  # ~948KB (reserve tail for stream rings)

# Streaming obs/action rings (SPSC)
IPC_STREAM_MAGIC     = 0x5354524D  # "STRM"
IPC_OBS_RING_BYTES   = 0x1000
IPC_ACT_RING_BYTES   = 0x1000
IPC_OBS_RING_OFFSET  = IPC_HEAP_DATA_OFFSET + IPC_HEAP_DATA_SIZE
IPC_ACT_RING_OFFSET  = IPC_OBS_RING_OFFSET + IPC_OBS_RING_BYTES
IPC_OBS_RING_SIZE    = 64
IPC_ACT_RING_SIZE    = 64

IPC_SHARED_MEM_SIZE  = 0x100000  # 1MB total

# =============================================================================
# MAGIC NUMBERS
# =============================================================================

IPC_MAGIC      = 0x51DECA9E  # "SIDEAR" - sort of
IPC_RSP_MAGIC  = 0x52535030  # "RSP0"
IPC_HEAP_MAGIC = 0x48454150  # "HEAP"
BLOB_MAGIC     = 0x424C4F42  # "BLOB"
DOORBELL_MAGIC = 0x444F4F52  # "DOOR"

# =============================================================================
# RING BUFFER CONSTANTS
# =============================================================================

IPC_RING_SIZE = 1024  # Number of packets in ring
RING_HEADER_SIZE = 32  # 4 uint32 + 4 reserved uint32

# =============================================================================
# COMMAND IDs (0x0000-0x7FFF)
# =============================================================================

CMD_PING      = 0x0001
CMD_PRINT     = 0x0002
CMD_RUN_MODEL = 0x0010
CMD_ENV_RESET = 0x0100
CMD_ENV_STEP  = 0x0101
CMD_IFR_PERSIST = 0x0200
CMD_ARB_EPISODE = 0x0201
CMD_TELEMETRY_POLL = 0x0300

# CMD_ENV_RESET payload flags
ENV_RESET_FLAG_STREAM = 0x00000001

# CMD_ENV_STEP payload encoding (single-trip control loop)
# [31:16] = ack blob id, [15:0] = action
ENV_STEP_ACTION_MASK = 0x0000FFFF
ENV_STEP_ACK_SHIFT = 16

def env_step_pack(action: int, ack_blob_id: int) -> int:
    return ((ack_blob_id & 0xFFFF) << ENV_STEP_ACK_SHIFT) | (action & ENV_STEP_ACTION_MASK)

def env_step_unpack(payload: int) -> Tuple[int, int]:
    action = payload & ENV_STEP_ACTION_MASK
    ack_blob_id = (payload >> ENV_STEP_ACK_SHIFT) & 0xFFFF
    return action, ack_blob_id

# Command names for logging
CMD_NAMES = {
    CMD_PING: "PING",
    CMD_PRINT: "PRINT",
    CMD_RUN_MODEL: "RUN_MODEL",
    CMD_ENV_RESET: "ENV_RESET",
    CMD_ENV_STEP: "ENV_STEP",
    CMD_IFR_PERSIST: "IFR_PERSIST",
    CMD_ARB_EPISODE: "ARB_EPISODE",
    CMD_TELEMETRY_POLL: "TELEMETRY_POLL",
}

# =============================================================================
# RESPONSE IDs (0x8000-0xFFFF) - high bit set indicates response
# =============================================================================

RSP_OK    = 0x8000
RSP_ERROR = 0x8001
RSP_BUSY  = 0x8002

RSP_NAMES = {
    RSP_OK: "OK",
    RSP_ERROR: "ERROR",
    RSP_BUSY: "BUSY",
}

# =============================================================================
# FLAGS
# =============================================================================

FLAG_IRQ_ON_COMPLETE = 0x0001  # Request interrupt on completion

# Doorbell flags
DOORBELL_FLAG_IRQ_ENABLED = 0x01
DOORBELL_FLAG_PENDING     = 0x02

# Blob types
BLOB_TYPE_RAW       = 0x00
BLOB_TYPE_TENSOR    = 0x01
BLOB_TYPE_MODEL_REF = 0x02
BLOB_TYPE_RESULT    = 0x03

# Blob flags
BLOB_FLAG_PINNED   = 0x01
BLOB_FLAG_READONLY = 0x02

# =============================================================================
# DATA TYPES (for tensors)
# =============================================================================

DTYPE_FLOAT32 = 0x00
DTYPE_FLOAT16 = 0x01
DTYPE_INT32   = 0x02
DTYPE_INT16   = 0x03
DTYPE_INT8    = 0x04
DTYPE_UINT8   = 0x05

# Map to numpy dtype strings
DTYPE_TO_NUMPY = {
    DTYPE_FLOAT32: 'float32',
    DTYPE_FLOAT16: 'float16',
    DTYPE_INT32:   'int32',
    DTYPE_INT16:   'int16',
    DTYPE_INT8:    'int8',
    DTYPE_UINT8:   'uint8',
}

# Map numpy dtype to protocol dtype
NUMPY_TO_DTYPE = {v: k for k, v in DTYPE_TO_NUMPY.items()}

# Dtype sizes in bytes
DTYPE_SIZES = {
    DTYPE_FLOAT32: 4,
    DTYPE_FLOAT16: 2,
    DTYPE_INT32:   4,
    DTYPE_INT16:   2,
    DTYPE_INT8:    1,
    DTYPE_UINT8:   1,
}

# =============================================================================
# HEAP CONSTANTS
# =============================================================================

HEAP_BLOCK_SHIFT = 6  # 64 byte minimum block
HEAP_BLOCK_SIZE  = 1 << HEAP_BLOCK_SHIFT  # 64 bytes
HEAP_MAX_BLOCKS  = IPC_HEAP_DATA_SIZE // HEAP_BLOCK_SIZE
HEAP_BITMAP_SIZE = (HEAP_MAX_BLOCKS + 7) // 8

# =============================================================================
# STRUCT FORMATS (little-endian)
# =============================================================================

# Ring buffer header: magic, head, tail, size, reserved[4]
# typedef struct {
#   uint32_t magic;
#   uint32_t head;
#   uint32_t tail;
#   uint32_t size;
#   uint32_t reserved[4];
#   ... data[]
# }
RING_HEADER_FMT = '<IIII4I'
RING_HEADER_STRUCT = struct.Struct(RING_HEADER_FMT)

# Command packet: cmd, flags, payload_id, timestamp
# typedef struct {
#   uint16_t cmd;
#   uint16_t flags;
#   uint32_t payload_id;
#   uint64_t timestamp;
# }
PACKET_FMT = '<HHIQ'
PACKET_STRUCT = struct.Struct(PACKET_FMT)
PACKET_SIZE = PACKET_STRUCT.size  # 16 bytes

# Response packet: status, orig_cmd, result, timestamp
# typedef struct {
#   uint16_t status;
#   uint16_t orig_cmd;
#   uint32_t result;
#   uint64_t timestamp;
# }
RESPONSE_FMT = '<HHIQ'
RESPONSE_STRUCT = struct.Struct(RESPONSE_FMT)
RESPONSE_SIZE = RESPONSE_STRUCT.size  # 16 bytes

# Streaming ring entries
# obs_entry_t: seq, obs[4], reward, done, model_id
OBS_ENTRY_FMT = '<I4ffff'
OBS_ENTRY_STRUCT = struct.Struct(OBS_ENTRY_FMT)
OBS_ENTRY_SIZE = OBS_ENTRY_STRUCT.size  # 32 bytes

# action_entry_t: seq, action, flags, ack_seq, reserved
ACT_ENTRY_FMT = '<IHHII'
ACT_ENTRY_STRUCT = struct.Struct(ACT_ENTRY_FMT)
ACT_ENTRY_SIZE = ACT_ENTRY_STRUCT.size  # 16 bytes

# IFR record (136 bytes)
IFR_MAGIC = 0x30465249  # "IFR0"
IFR_VERSION = 2
IFR_PROFILE_MAX = 16
# magic, version, flags, job_id, episode_id, model_id, record_size, ts_usec,
# goodput, profile_len, reserved, profile[16], hash[32]
IFR_FMT = '<IHHIIIIQfHH16f32s'
IFR_STRUCT = struct.Struct(IFR_FMT)
IFR_SIZE = IFR_STRUCT.size
IFR_HASH_OFFSET = IFR_SIZE - 32

# Telemetry snapshot
TELEMETRY_FMT = '<Qfff'
TELEMETRY_STRUCT = struct.Struct(TELEMETRY_FMT)
TELEMETRY_SIZE = TELEMETRY_STRUCT.size

# Doorbell control block (256 bytes)
# typedef struct {
#   uint32_t magic;
#   uint32_t version;
#   volatile uint32_t cmd_doorbell;
#   volatile uint32_t cmd_flags;
#   volatile uint32_t cmd_irq_count;
#   volatile uint32_t rsp_doorbell;
#   volatile uint32_t rsp_flags;
#   volatile uint32_t rsp_irq_count;
#   volatile uint32_t cmd_writes;
#   volatile uint32_t rsp_writes;
#   uint32_t reserved[54];
# }
DOORBELL_FMT = '<IIIIIIIIII54I'
DOORBELL_STRUCT = struct.Struct(DOORBELL_FMT)

# Blob header (32 bytes)
# typedef struct {
#   uint32_t magic;
#   uint16_t blob_id;
#   uint8_t  type;
#   uint8_t  flags;
#   uint32_t size;
#   uint32_t offset;
#   uint32_t checksum;
#   uint32_t reserved[3];
# }
BLOB_HEADER_FMT = '<IHBBIII3I'
BLOB_HEADER_STRUCT = struct.Struct(BLOB_HEADER_FMT)
BLOB_HEADER_SIZE = BLOB_HEADER_STRUCT.size  # 32 bytes

# Tensor header (40 bytes, embedded after blob header for BLOB_TYPE_TENSOR)
# typedef struct {
#   uint8_t  dtype;
#   uint8_t  ndim;
#   uint16_t reserved;
#   uint32_t shape[4];
#   uint32_t strides[4];
# }
TENSOR_HEADER_FMT = '<BBH4I4I'
TENSOR_HEADER_STRUCT = struct.Struct(TENSOR_HEADER_FMT)
TENSOR_HEADER_SIZE = TENSOR_HEADER_STRUCT.size  # 40 bytes

# Heap control block
# typedef struct {
#   uint32_t magic;
#   uint32_t version;
#   uint32_t total_blocks;
#   uint32_t free_blocks;
#   uint32_t next_blob_id;
#   uint32_t reserved[3];
#   uint8_t  bitmap[];
# }
HEAP_CTL_FMT = '<IIIII3I'
HEAP_CTL_STRUCT = struct.Struct(HEAP_CTL_FMT)
HEAP_CTL_HEADER_SIZE = HEAP_CTL_STRUCT.size  # 32 bytes


# =============================================================================
# DATA CLASSES
# =============================================================================

@dataclass
class RingHeader:
    magic: int
    head: int
    tail: int
    size: int

    @classmethod
    def unpack(cls, data: bytes) -> 'RingHeader':
        magic, head, tail, size, *_ = RING_HEADER_STRUCT.unpack(data[:RING_HEADER_STRUCT.size])
        return cls(magic, head, tail, size)


@dataclass
class Packet:
    cmd: int
    flags: int
    payload_id: int
    timestamp: int

    @classmethod
    def unpack(cls, data: bytes) -> 'Packet':
        return cls(*PACKET_STRUCT.unpack(data[:PACKET_SIZE]))

    def pack(self) -> bytes:
        return PACKET_STRUCT.pack(self.cmd, self.flags, self.payload_id, self.timestamp)


@dataclass
class Response:
    status: int
    orig_cmd: int
    result: int
    timestamp: int

    @classmethod
    def unpack(cls, data: bytes) -> 'Response':
        return cls(*RESPONSE_STRUCT.unpack(data[:RESPONSE_SIZE]))

    def pack(self) -> bytes:
        return RESPONSE_STRUCT.pack(self.status, self.orig_cmd, self.result, self.timestamp)


@dataclass
class BlobHeader:
    magic: int
    blob_id: int
    type: int
    flags: int
    size: int
    offset: int
    checksum: int

    @classmethod
    def unpack(cls, data: bytes) -> 'BlobHeader':
        magic, blob_id, type_, flags, size, offset, checksum, *_ = \
            BLOB_HEADER_STRUCT.unpack(data[:BLOB_HEADER_SIZE])
        return cls(magic, blob_id, type_, flags, size, offset, checksum)

    def pack(self) -> bytes:
        return BLOB_HEADER_STRUCT.pack(
            self.magic, self.blob_id, self.type, self.flags,
            self.size, self.offset, self.checksum, 0, 0, 0
        )


@dataclass
class TensorHeader:
    dtype: int
    ndim: int
    shape: Tuple[int, ...]
    strides: Tuple[int, ...]

    @classmethod
    def unpack(cls, data: bytes) -> 'TensorHeader':
        values = TENSOR_HEADER_STRUCT.unpack(data[:TENSOR_HEADER_SIZE])
        dtype = values[0]
        ndim = values[1]
        # reserved = values[2]
        shape = values[3:7]
        strides = values[7:11]
        return cls(dtype, ndim, shape[:ndim], strides[:ndim])

    def pack(self) -> bytes:
        # Pad shape and strides to 4 elements
        shape_padded = tuple(self.shape) + (0,) * (4 - len(self.shape))
        strides_padded = tuple(self.strides) + (0,) * (4 - len(self.strides))
        return TENSOR_HEADER_STRUCT.pack(
            self.dtype, self.ndim, 0,  # reserved
            *shape_padded, *strides_padded
        )


@dataclass
class HeapControl:
    magic: int
    version: int
    total_blocks: int
    free_blocks: int
    next_blob_id: int

    @classmethod
    def unpack(cls, data: bytes) -> 'HeapControl':
        magic, version, total_blocks, free_blocks, next_blob_id, *_ = \
            HEAP_CTL_STRUCT.unpack(data[:HEAP_CTL_HEADER_SIZE])
        return cls(magic, version, total_blocks, free_blocks, next_blob_id)


# =============================================================================
# UTILITY FUNCTIONS
# =============================================================================

def get_timestamp() -> int:
    """Get current timestamp in microseconds (matches ZENEDGE time_us())."""
    import time
    return int(time.time() * 1_000_000)


def compute_checksum(data: bytes) -> int:
    """Simple checksum matching ZENEDGE heap implementation."""
    checksum = 0
    for b in data:
        checksum = (checksum + b) & 0xFFFFFFFF
    return checksum
