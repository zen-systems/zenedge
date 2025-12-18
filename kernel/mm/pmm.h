/* kernel/mm/pmm.h
 *
 * Physical Memory Manager (PMM) for ZENEDGE
 *
 * Uses a bitmap-based allocator for physical page frames.
 * Each bit represents a 4KB page (0=free, 1=used).
 *
 * Designed with NUMA awareness in mind - currently single-node
 * but the API supports future multi-node expansion.
 */
#ifndef ZENEDGE_PMM_H
#define ZENEDGE_PMM_H

#include <stdint.h>
#include <stddef.h>

/* Page size: 4KB */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

/* Maximum physical memory we can manage (256MB for now) */
#define PMM_MAX_MEMORY  (256 * 1024 * 1024)
#define PMM_MAX_PAGES   (PMM_MAX_MEMORY / PAGE_SIZE)

/* NUMA node IDs
 * We simulate 2 nodes by splitting physical memory in half:
 * - Node 0: "local" / latency-sensitive (lower half)
 * - Node 1: "remote" / background work (upper half)
 */
#define NUMA_NODE_LOCAL  0
#define NUMA_NODE_REMOTE 1
#define NUMA_NODE_ANY    0xFF  /* Let PMM choose */
#define NUMA_MAX_NODES   2

/* Physical address type */
typedef uint32_t paddr_t;

/* Memory region types (from multiboot) */
typedef enum {
    MEM_REGION_AVAILABLE = 1,
    MEM_REGION_RESERVED  = 2,
    MEM_REGION_ACPI_RECLAIMABLE = 3,
    MEM_REGION_ACPI_NVS  = 4,
    MEM_REGION_BAD       = 5
} mem_region_type_t;

/* Memory region descriptor */
typedef struct {
    paddr_t base;
    uint32_t length;
    mem_region_type_t type;
} mem_region_t;

/* NUMA node descriptor */
typedef struct {
    uint8_t  node_id;
    paddr_t  base_addr;
    uint32_t start_pfn;     /* First PFN in this node */
    uint32_t end_pfn;       /* Last PFN + 1 in this node */
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t used_pages;
} numa_node_t;

/* PMM statistics */
typedef struct {
    uint32_t total_memory_kb;
    uint32_t free_memory_kb;
    uint32_t used_memory_kb;
    uint32_t reserved_memory_kb;
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t num_regions;
    uint32_t num_nodes;
} pmm_stats_t;

/* Multiboot memory map entry (as provided by GRUB) */
typedef struct __attribute__((packed)) {
    uint32_t size;      /* Size of this entry (excluding this field) */
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} multiboot_mmap_entry_t;

/* Multiboot info structure (partial - just what we need) */
typedef struct __attribute__((packed)) {
    uint32_t flags;
    uint32_t mem_lower;     /* KB of lower memory (below 1MB) */
    uint32_t mem_upper;     /* KB of upper memory (above 1MB) */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} multiboot_info_t;

/* Multiboot flags */
#define MULTIBOOT_FLAG_MEM      (1 << 0)
#define MULTIBOOT_FLAG_MMAP     (1 << 6)

/*
 * Initialize the physical memory manager
 * @param mboot_info: Pointer to multiboot info structure
 */
void pmm_init(multiboot_info_t *mboot_info);

/*
 * Allocate a single physical page
 * @param node: NUMA node preference (NUMA_NODE_LOCAL for any)
 * @return: Physical address of allocated page, or 0 on failure
 */
paddr_t pmm_alloc_page(uint8_t node);

/*
 * Allocate contiguous physical pages
 * @param count: Number of pages to allocate
 * @param node: NUMA node preference
 * @return: Physical address of first page, or 0 on failure
 */
paddr_t pmm_alloc_pages(uint32_t count, uint8_t node);

/*
 * Free a single physical page
 * @param addr: Physical address of page to free
 */
void pmm_free_page(paddr_t addr);

/*
 * Free contiguous physical pages
 * @param addr: Physical address of first page
 * @param count: Number of pages to free
 */
void pmm_free_pages(paddr_t addr, uint32_t count);

/*
 * Get PMM statistics
 * @param stats: Pointer to stats structure to fill
 */
void pmm_get_stats(pmm_stats_t *stats);

/*
 * Get NUMA node info
 * @param node_id: Node ID to query
 * @return: Pointer to node descriptor, or NULL if invalid
 */
numa_node_t *pmm_get_node(uint8_t node_id);

/*
 * Get number of NUMA nodes
 */
uint8_t pmm_get_node_count(void);

/*
 * Get which node a physical address belongs to
 */
uint8_t pmm_addr_to_node(paddr_t addr);

/*
 * Mark a range of physical memory as reserved
 * Used for kernel, modules, MMIO regions, etc.
 * @param base: Start address (will be page-aligned down)
 * @param length: Length in bytes (will be page-aligned up)
 */
void pmm_reserve_range(paddr_t base, uint32_t length);

/*
 * Debug: dump memory map to console
 */
void pmm_dump_map(void);

/* Convert between addresses and page frame numbers */
static inline uint32_t addr_to_pfn(paddr_t addr) {
    return addr >> PAGE_SHIFT;
}

static inline paddr_t pfn_to_addr(uint32_t pfn) {
    return (paddr_t)(pfn << PAGE_SHIFT);
}

/* Page-align addresses */
static inline paddr_t page_align_down(paddr_t addr) {
    return addr & ~(PAGE_SIZE - 1);
}

static inline paddr_t page_align_up(paddr_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

#endif /* ZENEDGE_PMM_H */
