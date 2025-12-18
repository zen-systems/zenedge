/* kernel/zenedge_alloc.h
 *
 * Portable memory allocation abstraction for ZENEDGE
 *
 * This layer abstracts the platform-specific PMM, allowing the job graph
 * and contract systems to be ported to other RTOSes (Zephyr, seL4, etc.)
 * without modification.
 *
 * Platform implementations must provide:
 *   - zenedge_platform_alloc_page()
 *   - zenedge_platform_free_page()
 *   - zenedge_platform_get_node_for_addr()
 */
#ifndef ZENEDGE_ALLOC_H
#define ZENEDGE_ALLOC_H

#include <stdint.h>
#include <stddef.h>

/* Platform-independent physical address type */
typedef uint64_t zphys_t;   /* 64-bit for future-proofing */

/* Allocation result with metadata */
typedef struct {
    zphys_t  addr;          /* Physical address (0 = failure) */
    uint8_t  node;          /* NUMA node where allocated */
    uint32_t size_bytes;    /* Actual size allocated */
} zalloc_result_t;

/* NUMA hints */
#define ZNODE_LOCAL     0
#define ZNODE_REMOTE    1
#define ZNODE_ANY       0xFF

/* Memory tier hints (for future HBM/device memory) */
typedef enum {
    ZTIER_DDR = 0,          /* Regular system RAM */
    ZTIER_HBM,              /* High-bandwidth memory */
    ZTIER_DEVICE,           /* Accelerator local memory */
    ZTIER_PINNED            /* DMA-capable, non-pageable */
} ztier_t;

/* Allocation flags */
#define ZALLOC_ZERO      (1 << 0)   /* Zero the memory */
#define ZALLOC_CONTIGUOUS (1 << 1)  /* Must be physically contiguous */
#define ZALLOC_PINNED    (1 << 2)   /* Pin in physical memory (no swap) */
#define ZALLOC_ALIGNED   (1 << 3)   /* Align to size boundary */

/* Allocation request */
typedef struct {
    uint32_t size_bytes;    /* Requested size */
    uint8_t  node_pref;     /* NUMA node preference */
    ztier_t  tier;          /* Memory tier preference */
    uint32_t flags;         /* ZALLOC_* flags */
    uint32_t alignment;     /* If ZALLOC_ALIGNED, alignment requirement */
} zalloc_request_t;

/*
 * Initialize the allocation subsystem
 * Called once at kernel startup after platform PMM is ready
 */
void zenedge_alloc_init(void);

/*
 * Allocate a single page (4KB)
 * @param node_pref: NUMA node preference (ZNODE_LOCAL, ZNODE_REMOTE, ZNODE_ANY)
 * @return: Allocation result (addr=0 on failure)
 */
zalloc_result_t zenedge_alloc_page(uint8_t node_pref);

/*
 * Allocate multiple contiguous pages
 * @param count: Number of pages
 * @param node_pref: NUMA node preference
 * @return: Allocation result for first page (addr=0 on failure)
 */
zalloc_result_t zenedge_alloc_pages(uint32_t count, uint8_t node_pref);

/*
 * Advanced allocation with full control
 * @param req: Allocation request structure
 * @return: Allocation result (addr=0 on failure)
 */
zalloc_result_t zenedge_alloc(const zalloc_request_t *req);

/*
 * Free a single page
 * @param addr: Physical address to free
 */
void zenedge_free_page(zphys_t addr);

/*
 * Free multiple contiguous pages
 * @param addr: Physical address of first page
 * @param count: Number of pages to free
 */
void zenedge_free_pages(zphys_t addr, uint32_t count);

/*
 * Get NUMA node for a physical address
 * @param addr: Physical address to query
 * @return: NUMA node ID
 */
uint8_t zenedge_get_node(zphys_t addr);

/*
 * Get total/free memory statistics
 */
typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t num_nodes;
} zalloc_stats_t;

void zenedge_alloc_stats(zalloc_stats_t *stats);

/* ========================================================================
 * Platform-specific functions (must be implemented per-platform)
 * ======================================================================== */

/*
 * Platform: Allocate a page
 * Implemented by: pmm_alloc_page() on bare-metal, k_mem_pool_alloc() on Zephyr, etc.
 */
zphys_t zenedge_platform_alloc_page(uint8_t node_pref);

/*
 * Platform: Free a page
 */
void zenedge_platform_free_page(zphys_t addr);

/*
 * Platform: Get NUMA node for address
 */
uint8_t zenedge_platform_get_node(zphys_t addr);

/*
 * Platform: Get memory statistics
 */
void zenedge_platform_get_stats(zalloc_stats_t *stats);

#endif /* ZENEDGE_ALLOC_H */
