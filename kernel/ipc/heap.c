/* kernel/ipc/heap.c - Shared Heap Implementation
 *
 * Simple bitmap-based allocator for the shared memory heap.
 * Used to pass tensor data between ZENEDGE and Linux bridge.
 */

#include "heap.h"
#include "../console.h"
#include "../mm/vmm.h"

/* External: shared memory base (set by ipc_init) */
extern uint32_t ipc_shm_base;

/* Heap pointers (set during heap_init) */
static volatile heap_ctl_t *heap_ctl = NULL;
static volatile uint8_t *heap_data = NULL;

/* Blob table - maps blob_id to offset (simple linear search for now) */
#define MAX_BLOBS 256
static struct {
  uint16_t blob_id;
  uint32_t offset; /* Offset from heap_data base */
  uint32_t blocks; /* Number of blocks allocated */
} blob_table[MAX_BLOBS];
static uint32_t blob_count = 0;

/* Helper: set bit in bitmap */
static void bitmap_set(uint32_t block) {
  if (block < HEAP_MAX_BLOCKS) {
    heap_ctl->bitmap[block / 8] |= (1 << (block % 8));
  }
}

/* Helper: clear bit in bitmap */
static void bitmap_clear(uint32_t block) {
  if (block < HEAP_MAX_BLOCKS) {
    heap_ctl->bitmap[block / 8] &= ~(1 << (block % 8));
  }
}

/* Helper: test bit in bitmap */
static int bitmap_test(uint32_t block) {
  if (block >= HEAP_MAX_BLOCKS)
    return 1; /* Out of range = used */
  return (heap_ctl->bitmap[block / 8] >> (block % 8)) & 1;
}

/* Helper: find contiguous free blocks */
static uint32_t find_free_blocks(uint32_t count) {
  uint32_t start = 0;
  uint32_t run = 0;

  for (uint32_t i = 0; i < HEAP_MAX_BLOCKS; i++) {
    if (!bitmap_test(i)) {
      if (run == 0)
        start = i;
      run++;
      if (run >= count)
        return start;
    } else {
      run = 0;
    }
  }
  return (uint32_t)-1; /* Not found */
}

/* Helper: simple checksum */
static uint32_t compute_checksum(const void *data, uint32_t size) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t sum = 0;
  for (uint32_t i = 0; i < size; i++) {
    sum = (sum << 1) | (sum >> 31); /* Rotate left */
    sum ^= p[i];
  }
  return sum;
}

void heap_init(void *heap_base) {
  if (heap_base == NULL)
    return;

  heap_ctl = (heap_ctl_t *)heap_base;
  /* Data starts relative to control block */
  heap_data = (uint8_t *)heap_base + (IPC_HEAP_DATA_OFFSET - IPC_HEAP_CTL_OFFSET);

  console_write("[heap] initializing shared heap at ");
  print_hex32((uint32_t)heap_ctl);
  console_write("\n");

  heap_ctl->magic = IPC_HEAP_MAGIC;
  heap_ctl->version = 1;
  heap_ctl->total_blocks = HEAP_MAX_BLOCKS;
  heap_ctl->free_blocks = HEAP_MAX_BLOCKS;
  heap_ctl->next_blob_id = 1; /* Start ID at 1 */

  /* Clear bitmap (all free) */
  for (uint32_t i = 0; i < HEAP_BITMAP_SIZE; i++) {
    heap_ctl->bitmap[i] = 0;
  }

  /* Clear blob table */
  blob_count = 0;
  for (uint32_t i = 0; i < MAX_BLOBS; i++) {
    blob_table[i].blob_id = 0;
  }

  console_write("[heap] initialized: ");
  print_uint(IPC_HEAP_DATA_SIZE / 1024);
  console_write("KB, ");
  print_uint(HEAP_MAX_BLOCKS);
  console_write(" blocks\n");
}

uint16_t heap_alloc(uint32_t size, uint8_t type) {
  if (!heap_ctl || heap_ctl->magic != IPC_HEAP_MAGIC)
    return 0;

  /* Add space for blob header */
  uint32_t total_size = size + sizeof(heap_blob_t);

  /* Calculate blocks needed */
  uint32_t blocks = (total_size + HEAP_BLOCK_SIZE - 1) / HEAP_BLOCK_SIZE;

  /* Find free blocks */
  uint32_t start = find_free_blocks(blocks);
  if (start == (uint32_t)-1) {
    console_write("[heap] alloc failed: no space for ");
    print_uint(blocks);
    console_write(" blocks\n");
    return 0;
  }

  /* Mark blocks as used */
  for (uint32_t i = 0; i < blocks; i++) {
    bitmap_set(start + i);
  }
  heap_ctl->free_blocks -= blocks;

  /* Assign blob ID */
  uint16_t blob_id = heap_ctl->next_blob_id++;
  if (heap_ctl->next_blob_id == 0)
    heap_ctl->next_blob_id = 1;

  /* Calculate offset */
  uint32_t offset = start * HEAP_BLOCK_SIZE;

  /* Record in blob table */
  if (blob_count < MAX_BLOBS) {
    blob_table[blob_count].blob_id = blob_id;
    blob_table[blob_count].offset = offset;
    blob_table[blob_count].blocks = blocks;
    blob_count++;
  }

  /* Initialize blob header */
  heap_blob_t *blob = (heap_blob_t *)(heap_data + offset);
  blob->magic = BLOB_MAGIC;
  blob->blob_id = blob_id;
  blob->type = type;
  blob->flags = 0;
  blob->size = size;
  blob->offset = offset + sizeof(heap_blob_t);
  blob->checksum = 0;

  return blob_id;
}

void heap_free(uint16_t blob_id) {
  if (!heap_ctl || blob_id == 0)
    return;

  /* Find in blob table */
  for (uint32_t i = 0; i < blob_count; i++) {
    if (blob_table[i].blob_id == blob_id) {
      uint32_t start = blob_table[i].offset / HEAP_BLOCK_SIZE;
      uint32_t blocks = blob_table[i].blocks;

      /* Clear blocks in bitmap */
      for (uint32_t j = 0; j < blocks; j++) {
        bitmap_clear(start + j);
      }
      heap_ctl->free_blocks += blocks;

      /* Remove from table (swap with last) */
      blob_table[i] = blob_table[blob_count - 1];
      blob_count--;

      return;
    }
  }
}

heap_blob_t *heap_get_blob(uint16_t blob_id) {
  if (!heap_ctl || blob_id == 0)
    return NULL;

  /* 1. Check Cache */
  for (uint32_t i = 0; i < blob_count; i++) {
    if (blob_table[i].blob_id == blob_id) {
      heap_blob_t *blob = (heap_blob_t *)(heap_data + blob_table[i].offset);
      /* Verify it's still there (might have been freed remotely) */
      if (blob->magic == BLOB_MAGIC && blob->blob_id == blob_id) {
        return blob;
      }
    }
  }

  /* 2. Scan Heap (Slow Path for Remote/First Access) */
  /* Iterate all blocks to find the blob header */
  /* Optimization: Current max alloc? For now, scan all. */
  
  /* console_write("[heap] Blob not in cache, scanning...\n"); */
  
  for (uint32_t offset = 0; offset < IPC_HEAP_DATA_SIZE; offset += HEAP_BLOCK_SIZE) {
      heap_blob_t *blob = (heap_blob_t *)(heap_data + offset);
      if (blob->magic == BLOB_MAGIC) {
          /* Found a valid blob */
          if (blob->blob_id == blob_id) {
              /* Add to cache */
              if (blob_count < MAX_BLOBS) {
                  blob_table[blob_count].blob_id = blob_id;
                  blob_table[blob_count].offset = offset;
                  /* Blocks = (size + hdr + block_size - 1) / block_size */
                  uint32_t total = blob->size + sizeof(heap_blob_t);
                  blob_table[blob_count].blocks = (total + HEAP_BLOCK_SIZE - 1) / HEAP_BLOCK_SIZE;
                  blob_count++;
              }
              return blob;
          }
          
          /* Skip ahead? blob->size is set.
           * We can jump, but need to be careful of alignment/padding.
           * Let's check size to jump faster.
           */
           uint32_t total = blob->size + sizeof(heap_blob_t);
           uint32_t blocks = (total + HEAP_BLOCK_SIZE - 1) / HEAP_BLOCK_SIZE;
           if (blocks > 1) {
               offset += (blocks - 1) * HEAP_BLOCK_SIZE;
           }
      }
  }

  return NULL;
}

void *heap_get_data(uint16_t blob_id) {
  heap_blob_t *blob = heap_get_blob(blob_id);
  if (!blob)
    return NULL;
  return (void *)(heap_data + blob->offset);
}

void heap_get_stats(heap_stats_t *stats) {
  if (!stats)
    return;

  if (!heap_ctl || heap_ctl->magic != IPC_HEAP_MAGIC) {
    stats->total_bytes = 0;
    stats->free_bytes = 0;
    stats->used_bytes = 0;
    stats->total_blocks = 0;
    stats->free_blocks = 0;
    stats->blob_count = 0;
    return;
  }

  stats->total_blocks = heap_ctl->total_blocks;
  stats->free_blocks = heap_ctl->free_blocks;
  stats->total_bytes = heap_ctl->total_blocks * HEAP_BLOCK_SIZE;
  stats->free_bytes = heap_ctl->free_blocks * HEAP_BLOCK_SIZE;
  stats->used_bytes = stats->total_bytes - stats->free_bytes;
  stats->blob_count = blob_count;
}

void heap_dump_debug(void) {
  console_write("[heap] === DEBUG DUMP ===\n");

  if (!heap_ctl || heap_ctl->magic != IPC_HEAP_MAGIC) {
    console_write("[heap] NOT INITIALIZED\n");
    return;
  }

  console_write("[heap] Magic: ");
  print_hex32(heap_ctl->magic);
  console_write(" (valid)\n");

  console_write("[heap] Blocks: ");
  print_uint(heap_ctl->free_blocks);
  console_write("/");
  print_uint(heap_ctl->total_blocks);
  console_write(" free\n");

  console_write("[heap] Memory: ");
  print_uint((heap_ctl->total_blocks - heap_ctl->free_blocks) *
             HEAP_BLOCK_SIZE);
  console_write("/");
  print_uint(heap_ctl->total_blocks * HEAP_BLOCK_SIZE);
  console_write(" bytes used\n");

  console_write("[heap] Blobs: ");
  print_uint(blob_count);
  console_write(" allocated\n");

  /* List blobs */
  for (uint32_t i = 0; i < blob_count && i < 8; i++) {
    heap_blob_t *blob = (heap_blob_t *)(heap_data + blob_table[i].offset);
    console_write("  [");
    print_uint(blob->blob_id);
    console_write("] type=");
    print_uint(blob->type);
    console_write(" size=");
    print_uint(blob->size);
    console_write(" blocks=");
    print_uint(blob_table[i].blocks);
    console_write("\n");
  }
  if (blob_count > 8) {
    console_write("  ... and ");
    print_uint(blob_count - 8);
    console_write(" more\n");
  }

  console_write("[heap] === END DUMP ===\n");
}

/* Helper: get dtype size in bytes */
static uint32_t dtype_size(uint8_t dtype) {
  switch (dtype) {
  case DTYPE_FLOAT32:
    return 4;
  case DTYPE_FLOAT16:
    return 2;
  case DTYPE_INT32:
    return 4;
  case DTYPE_INT16:
    return 2;
  case DTYPE_INT8:
    return 1;
  case DTYPE_UINT8:
    return 1;
  default:
    return 1;
  }
}

uint16_t heap_alloc_tensor(uint8_t dtype, uint8_t ndim, const uint32_t *shape) {
  if (ndim == 0 || ndim > 4 || !shape)
    return 0;

  /* Calculate tensor data size */
  uint32_t nelems = 1;
  for (uint8_t i = 0; i < ndim; i++) {
    nelems *= shape[i];
  }
  uint32_t elem_size = dtype_size(dtype);
  uint32_t data_size = nelems * elem_size;

  /* Total size = tensor header + data */
  uint32_t total_size = sizeof(tensor_header_t) + data_size;

  /* Allocate blob */
  uint16_t blob_id = heap_alloc(total_size, BLOB_TYPE_TENSOR);
  if (blob_id == 0)
    return 0;

  /* Initialize tensor header */
  void *data = heap_get_data(blob_id);
  tensor_header_t *hdr = (tensor_header_t *)data;
  hdr->dtype = dtype;
  hdr->ndim = ndim;
  hdr->reserved = 0;

  /* Set shape and calculate strides (row-major) */
  uint32_t stride = elem_size;
  for (int i = ndim - 1; i >= 0; i--) {
    hdr->shape[i] = shape[i];
    hdr->strides[i] = stride;
    stride *= shape[i];
  }

  /* Zero remaining dimensions */
  for (int i = ndim; i < 4; i++) {
    hdr->shape[i] = 0;
    hdr->strides[i] = 0;
  }

  return blob_id;
}

void *heap_get_tensor_data(uint16_t blob_id) {
  heap_blob_t *blob = heap_get_blob(blob_id);
  if (!blob || blob->type != BLOB_TYPE_TENSOR)
    return NULL;

  /* ABI Verify: Magic matches */
  if (blob->magic != BLOB_MAGIC) { 
      console_write("[heap] Security: Invalid magic in blob header\n");
      return NULL;
  }

  /* ABI Verify: Bounds check */
  if (blob->offset + blob->size > IPC_HEAP_DATA_SIZE) {
      console_write("[heap] Security: Blob data out of bounds\n");
      return NULL;
  }
  
  void *data = heap_get_data(blob_id);
  if (!data)
    return NULL;
  
  if (blob->size < sizeof(tensor_header_t)) {
      console_write("[heap] Security: Blob too small for tensor header\n");
      return NULL;
  }
  
  /* Verify Tensor Header */
  tensor_header_t *hdr = (tensor_header_t *)data;
  if (hdr->ndim > 4) {
       console_write("[heap] Security: Invalid ndim\n");
       return NULL;
  }
  
  /* Validate data size matches shape */
  uint32_t expected_size = sizeof(tensor_header_t);
  uint32_t nelems = 1;
  for(int i=0; i<hdr->ndim; i++) nelems *= hdr->shape[i];
  expected_size += nelems * dtype_size(hdr->dtype);
  
  if (expected_size > blob->size) {
      console_write("[heap] Security: Tensor shape exceeds blob size\n");
      return NULL;
  }

  /* Skip tensor header */
  return (uint8_t *)data + sizeof(tensor_header_t);
}

/* Helper: get physical address of a blob */
/* Note: We need to know the physical base of the heap.
 * ivshmem_phys_base is static in drivers/ivshmem.c.
 * But heap.c doesn't know it directly.
 * We can assume heap_init was passed something mapped 1:1 or we need to pass phys base.
 * Actually, vmm_init mapped SHARED_MEM_PHYS to SHARED_MEM_VIRT.
 * So we can translate.
 */
#include "../mm/vmm.h"

uint32_t heap_get_blob_phys(uint16_t blob_id) {
    heap_blob_t *blob = heap_get_blob(blob_id);
    if (!blob) return 0;
    
    void *vaddr = (void*)(heap_data + blob->offset);
    return vmm_virt_to_phys((uint32_t)vaddr);
}

uint32_t heap_get_blob_size(uint16_t blob_id) {
    heap_blob_t *blob = heap_get_blob(blob_id);
    if (!blob) return 0;
    return blob->size;
}
