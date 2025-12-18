/* kernel/zenedge_alloc.c
 *
 * Portable memory allocation implementation for ZENEDGE bare-metal
 *
 * This file provides the platform-specific implementations that wrap
 * our existing PMM. When porting to another RTOS, only this file
 * needs to be replaced.
 */
#include "zenedge_alloc.h"
#include "mm/pmm.h"
#include "console.h"

/* Statistics tracking */
static uint64_t total_allocated = 0;
static uint64_t total_freed = 0;

void zenedge_alloc_init(void) {
    console_write("[zalloc] portable allocator initialized\n");
    total_allocated = 0;
    total_freed = 0;
}

/* ========================================================================
 * Public API implementations
 * ======================================================================== */

zalloc_result_t zenedge_alloc_page(uint8_t node_pref) {
    zalloc_result_t result = {0};

    result.addr = zenedge_platform_alloc_page(node_pref);
    if (result.addr) {
        result.node = zenedge_platform_get_node(result.addr);
        result.size_bytes = PAGE_SIZE;
        total_allocated += PAGE_SIZE;
    }

    return result;
}

zalloc_result_t zenedge_alloc_pages(uint32_t count, uint8_t node_pref) {
    zalloc_result_t result = {0};

    /* Map node preference */
    uint8_t pmm_node;
    switch (node_pref) {
        case ZNODE_LOCAL:  pmm_node = NUMA_NODE_LOCAL; break;
        case ZNODE_REMOTE: pmm_node = NUMA_NODE_REMOTE; break;
        default:           pmm_node = NUMA_NODE_ANY; break;
    }

    paddr_t addr = pmm_alloc_pages(count, pmm_node);
    if (addr) {
        result.addr = addr;
        result.node = zenedge_platform_get_node(addr);
        result.size_bytes = count * PAGE_SIZE;
        total_allocated += result.size_bytes;
    }

    return result;
}

zalloc_result_t zenedge_alloc(const zalloc_request_t *req) {
    zalloc_result_t result = {0};

    if (!req || req->size_bytes == 0) {
        return result;
    }

    /* Calculate pages needed */
    uint32_t pages = (req->size_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Handle alignment if requested */
    if (req->flags & ZALLOC_ALIGNED) {
        /* For now, we only support page-aligned allocations */
        /* Future: implement power-of-2 aligned allocations */
    }

    /* Allocate */
    if (pages == 1) {
        result = zenedge_alloc_page(req->node_pref);
    } else if (req->flags & ZALLOC_CONTIGUOUS) {
        result = zenedge_alloc_pages(pages, req->node_pref);
    } else {
        /* Non-contiguous multi-page: just allocate first page for now */
        /* Future: return scatter-gather list */
        result = zenedge_alloc_pages(pages, req->node_pref);
    }

    /* Zero if requested */
    if ((result.addr != 0) && (req->flags & ZALLOC_ZERO)) {
        /* Convert physical to virtual for zeroing */
        /* Note: This assumes identity mapping or known kernel mapping */
        uint8_t *vaddr = (uint8_t *)(result.addr + 0xC0000000); /* KERNEL_VBASE */
        for (uint32_t i = 0; i < result.size_bytes; i++) {
            vaddr[i] = 0;
        }
    }

    return result;
}

void zenedge_free_page(zphys_t addr) {
    if (addr) {
        zenedge_platform_free_page(addr);
        total_freed += PAGE_SIZE;
    }
}

void zenedge_free_pages(zphys_t addr, uint32_t count) {
    if (addr && count > 0) {
        pmm_free_pages((paddr_t)addr, count);
        total_freed += count * PAGE_SIZE;
    }
}

uint8_t zenedge_get_node(zphys_t addr) {
    return zenedge_platform_get_node(addr);
}

void zenedge_alloc_stats(zalloc_stats_t *stats) {
    if (!stats) return;
    zenedge_platform_get_stats(stats);
}

/* ========================================================================
 * Platform-specific implementations (bare-metal PMM wrappers)
 * ======================================================================== */

zphys_t zenedge_platform_alloc_page(uint8_t node_pref) {
    /* Map portable node preference to PMM node */
    uint8_t pmm_node;
    switch (node_pref) {
        case ZNODE_LOCAL:  pmm_node = NUMA_NODE_LOCAL; break;
        case ZNODE_REMOTE: pmm_node = NUMA_NODE_REMOTE; break;
        default:           pmm_node = NUMA_NODE_ANY; break;
    }

    return (zphys_t)pmm_alloc_page(pmm_node);
}

void zenedge_platform_free_page(zphys_t addr) {
    pmm_free_page((paddr_t)addr);
}

uint8_t zenedge_platform_get_node(zphys_t addr) {
    return pmm_addr_to_node((paddr_t)addr);
}

void zenedge_platform_get_stats(zalloc_stats_t *stats) {
    pmm_stats_t pmm_stats;
    pmm_get_stats(&pmm_stats);

    stats->total_bytes = (uint64_t)pmm_stats.total_memory_kb * 1024;
    stats->free_bytes = (uint64_t)pmm_stats.free_memory_kb * 1024;
    stats->num_nodes = pmm_stats.num_nodes;
}
