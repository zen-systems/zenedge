/* kernel/ipc/ipc_proto.h - Proxy Driver Protocol */

#ifndef _IPC_PROTO_H
#define _IPC_PROTO_H

#include <stdint.h>

#define IPC_MAGIC      0x51DECA9E /* "SIDEAR" - sort of */
#define IPC_RSP_MAGIC  0x52535030 /* "RSP0" */
#define IPC_HEAP_MAGIC 0x48454150 /* "HEAP" */
#define IPC_RING_SIZE  1024       /* Number of packets in ring */

/* =============================================================================
 * SHARED MEMORY LAYOUT (1MB total at 0x02000000)
 * =============================================================================
 * 0x00000 - 0x07FFF: Command ring (32KB)    - ZENEDGE -> Linux
 * 0x08000 - 0x0FFFF: Response ring (32KB)   - Linux -> ZENEDGE
 * 0x10000 - 0x100FF: Doorbell control (256B) - Interrupt signaling
 * 0x10100 - 0x10FFF: Heap control block (~4KB)
 * 0x11000 - 0xFFFFF: Heap data region (~956KB for tensors/models)
 */
#define IPC_CMD_RING_OFFSET  0x00000
#define IPC_RSP_RING_OFFSET  0x08000
#define IPC_DOORBELL_OFFSET  0x10000
#define IPC_HEAP_CTL_OFFSET  0x10100
#define IPC_HEAP_DATA_OFFSET 0x11000
#define IPC_HEAP_DATA_SIZE   0xEF000  /* ~956KB */

/* =============================================================================
 * DOORBELL MECHANISM - Low-latency interrupt signaling
 * =============================================================================
 *
 * The doorbell provides interrupt-based notification between ZENEDGE and Linux.
 * Writing to a doorbell register signals the other side that work is available.
 *
 * Flow (ZENEDGE -> Linux command):
 *   1. ZENEDGE writes packet to cmd_ring
 *   2. ZENEDGE writes to cmd_doorbell (value = ring head)
 *   3. Linux receives interrupt (or polls doorbell)
 *   4. Linux processes commands up to doorbell value
 *
 * Flow (Linux -> ZENEDGE response):
 *   1. Linux writes response to rsp_ring
 *   2. Linux writes to rsp_doorbell (value = ring head)
 *   3. ZENEDGE receives IRQ (if enabled)
 *   4. ZENEDGE processes responses up to doorbell value
 */

#define IPC_DOORBELL_MAGIC 0x444F4F52  /* "DOOR" */

/* Doorbell flags */
#define DOORBELL_FLAG_IRQ_ENABLED  0x01  /* Enable IRQ on doorbell write */
#define DOORBELL_FLAG_PENDING      0x02  /* IRQ pending (set by writer, cleared by reader) */

/* Doorbell control block (256 bytes at IPC_DOORBELL_OFFSET) */
typedef struct {
  uint32_t magic;           /* IPC_DOORBELL_MAGIC */
  uint32_t version;         /* Protocol version (1) */

  /* Command doorbell (ZENEDGE -> Linux) */
  volatile uint32_t cmd_doorbell;   /* Written by ZENEDGE: cmd_ring head */
  volatile uint32_t cmd_flags;      /* DOORBELL_FLAG_* */
  volatile uint32_t cmd_irq_count;  /* IRQ counter (debug) */

  /* Response doorbell (Linux -> ZENEDGE) */
  volatile uint32_t rsp_doorbell;   /* Written by Linux: rsp_ring head */
  volatile uint32_t rsp_flags;      /* DOORBELL_FLAG_* */
  volatile uint32_t rsp_irq_count;  /* IRQ counter (debug) */

  /* Statistics */
  volatile uint32_t cmd_writes;     /* Total cmd doorbell writes */
  volatile uint32_t rsp_writes;     /* Total rsp doorbell writes */

  uint32_t reserved[54];            /* Pad to 256 bytes */
} doorbell_ctl_t;

/* Heap block sizes (power of 2, minimum 64 bytes) */
#define HEAP_BLOCK_SHIFT     6        /* 64 byte minimum block */
#define HEAP_BLOCK_SIZE      (1 << HEAP_BLOCK_SHIFT)
#define HEAP_MAX_BLOCKS      (IPC_HEAP_DATA_SIZE / HEAP_BLOCK_SIZE)

/* Command IDs (0x0000-0x7FFF) */
#define CMD_PING      0x0001
#define CMD_PRINT     0x0002
#define CMD_RUN_MODEL 0x0010

/* Response IDs (0x8000-0xFFFF) - high bit set indicates response */
#define RSP_OK        0x8000
#define RSP_ERROR     0x8001
#define RSP_BUSY      0x8002

/* Flag bits */
#define FLAG_IRQ_ON_COMPLETE 0x0001  /* Request interrupt on completion */

/* Command Packet Structure (16 bytes) */
typedef struct {
  uint16_t cmd;        /* Command ID */
  uint16_t flags;      /* Flags (e.g., FLAG_IRQ_ON_COMPLETE) */
  uint32_t payload_id; /* ID of data/model in shared heap */
  uint64_t timestamp;  /* Timestamp for latency tracking */
} ipc_packet_t;

/* Response Packet Structure (16 bytes) */
typedef struct {
  uint16_t status;     /* Response status (RSP_OK, RSP_ERROR, etc.) */
  uint16_t orig_cmd;   /* Original command this responds to */
  uint32_t result;     /* Result value or error code */
  uint64_t timestamp;  /* Completion timestamp */
} ipc_response_t;

/* Ring Buffer Header (Shared Memory Control Block)
 * Located at start of each ring region.
 */
typedef struct {
  uint32_t magic;       /* Magic Signature */
  uint32_t head;        /* Producer Index */
  uint32_t tail;        /* Consumer Index */
  uint32_t size;        /* Size of ring (IPC_RING_SIZE) */
  uint32_t reserved[4]; /* Padding/Reserved */
  ipc_packet_t data[];  /* Ring Data */
} ipc_ring_t;

/* Response Ring uses same header but different data type */
typedef struct {
  uint32_t magic;       /* Magic Signature (IPC_RSP_MAGIC) */
  uint32_t head;        /* Producer Index (Written by Linux) */
  uint32_t tail;        /* Consumer Index (Written by ZENEDGE) */
  uint32_t size;        /* Size of ring (IPC_RING_SIZE) */
  uint32_t reserved[4]; /* Padding/Reserved */
  ipc_response_t data[];/* Ring Data */
} ipc_rsp_ring_t;

/* =============================================================================
 * SHARED HEAP - For passing tensor data between ZENEDGE and Linux
 * =============================================================================
 *
 * The heap uses a simple bitmap allocator with fixed-size blocks (64 bytes).
 * Larger allocations use multiple contiguous blocks.
 *
 * Usage pattern:
 *   1. ZENEDGE allocates blob: heap_alloc(size) -> blob_id
 *   2. ZENEDGE writes tensor data to blob
 *   3. ZENEDGE sends CMD_RUN_MODEL with blob_id as payload
 *   4. Linux reads blob, runs inference
 *   5. Linux writes result to same or new blob
 *   6. Linux sends response with result blob_id
 *   7. ZENEDGE reads result, frees blob(s)
 */

/* Blob types - what kind of data is in the blob */
#define BLOB_TYPE_RAW       0x00  /* Raw bytes */
#define BLOB_TYPE_TENSOR    0x01  /* Tensor with header */
#define BLOB_TYPE_MODEL_REF 0x02  /* Reference to model (path/ID) */
#define BLOB_TYPE_RESULT    0x03  /* Inference result */

/* Blob flags */
#define BLOB_FLAG_PINNED    0x01  /* Don't free automatically */
#define BLOB_FLAG_READONLY  0x02  /* Linux should not modify */

/* Blob descriptor (32 bytes) - stored at start of each allocation */
typedef struct {
  uint32_t magic;       /* 0x424C4F42 "BLOB" */
  uint16_t blob_id;     /* Unique ID for this blob */
  uint8_t  type;        /* BLOB_TYPE_* */
  uint8_t  flags;       /* BLOB_FLAG_* */
  uint32_t size;        /* Size of data (not including this header) */
  uint32_t offset;      /* Offset from heap base to data */
  uint32_t checksum;    /* Simple checksum for validation */
  uint32_t reserved[3]; /* Padding to 32 bytes */
} heap_blob_t;

#define BLOB_MAGIC 0x424C4F42 /* "BLOB" */

/* Tensor descriptor (embedded in blob data for BLOB_TYPE_TENSOR) */
typedef struct {
  uint8_t  dtype;       /* Data type (see below) */
  uint8_t  ndim;        /* Number of dimensions (max 4) */
  uint16_t reserved;
  uint32_t shape[4];    /* Dimension sizes */
  uint32_t strides[4];  /* Strides in bytes */
  /* Actual tensor data follows immediately */
} tensor_header_t;

/* Tensor data types */
#define DTYPE_FLOAT32  0x00
#define DTYPE_FLOAT16  0x01
#define DTYPE_INT32    0x02
#define DTYPE_INT16    0x03
#define DTYPE_INT8     0x04
#define DTYPE_UINT8    0x05

/* Heap control block - at IPC_HEAP_CTL_OFFSET */
typedef struct {
  uint32_t magic;           /* IPC_HEAP_MAGIC */
  uint32_t version;         /* Protocol version (1) */
  uint32_t total_blocks;    /* Total blocks available */
  uint32_t free_blocks;     /* Currently free blocks */
  uint32_t next_blob_id;    /* Next blob ID to assign */
  uint32_t reserved[3];     /* Padding */
  /* Bitmap follows: 1 bit per block (0=free, 1=used) */
  /* Size: (HEAP_MAX_BLOCKS + 7) / 8 bytes */
  uint8_t  bitmap[];
} heap_ctl_t;

/* Helper: calculate bitmap size in bytes */
#define HEAP_BITMAP_SIZE ((HEAP_MAX_BLOCKS + 7) / 8)

#endif /* _IPC_PROTO_H */
