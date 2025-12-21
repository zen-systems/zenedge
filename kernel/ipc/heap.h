/* kernel/ipc/heap.h - Shared Heap API for IPC */

#ifndef _IPC_HEAP_H
#define _IPC_HEAP_H

#include "ipc_proto.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the shared heap (called from ipc_init) */
void heap_init(void *heap_base);

/* Allocate a blob in the shared heap
 * size: number of bytes needed (will be rounded up to block size)
 * type: BLOB_TYPE_* constant
 * Returns: blob_id on success, 0 on failure (out of memory)
 */
uint16_t heap_alloc(uint32_t size, uint8_t type);

/* Free a previously allocated blob
 * blob_id: ID returned from heap_alloc
 */
void heap_free(uint16_t blob_id);

/* Get pointer to blob data (after the header)
 * blob_id: ID returned from heap_alloc
 * Returns: pointer to data region, or NULL if invalid
 */
void *heap_get_data(uint16_t blob_id);

/* Get blob descriptor
 * blob_id: ID returned from heap_alloc
 * Returns: pointer to blob header, or NULL if invalid
 */
heap_blob_t *heap_get_blob(uint16_t blob_id);

/* Get heap statistics */
typedef struct {
  uint32_t total_bytes;
  uint32_t free_bytes;
  uint32_t used_bytes;
  uint32_t total_blocks;
  uint32_t free_blocks;
  uint32_t blob_count;
} heap_stats_t;

void heap_get_stats(heap_stats_t *stats);

/* Debug: dump heap status to console */
void heap_dump_debug(void);

/* Helper: create a tensor blob
 * dtype: DTYPE_* constant
 * ndim: number of dimensions (1-4)
 * shape: array of dimension sizes
 * Returns: blob_id with tensor header initialized, 0 on failure
 */
uint16_t heap_alloc_tensor(uint8_t dtype, uint8_t ndim, const uint32_t *shape);

/* Helper: get tensor data pointer (skips tensor_header_t)
 * blob_id: ID of a BLOB_TYPE_TENSOR blob
 * Returns: pointer to raw tensor data, or NULL if invalid
 */
void *heap_get_tensor_data(uint16_t blob_id);

/* Get physical address of a blob's data (for mapping to user space) */
uint32_t heap_get_blob_phys(uint16_t blob_id);

/* Get size of a blob's data */
uint32_t heap_get_blob_size(uint16_t blob_id);

#ifdef __cplusplus
}
#endif

#endif /* _IPC_HEAP_H */
