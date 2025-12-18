/* kernel/mm/vmm.h
 *
 * Virtual Memory Manager (VMM) for ZENEDGE
 *
 * Implements x86 32-bit paging with:
 * - Two-level page tables (Page Directory + Page Tables)
 * - Higher-half kernel mapping at 0xC0000000
 * - 4KB pages (no PSE/large pages for simplicity)
 *
 * Virtual address space layout:
 *   0x00000000 - 0xBFFFFFFF: User space (3GB)
 *   0xC0000000 - 0xFFFFFFFF: Kernel space (1GB)
 */
#ifndef ZENEDGE_VMM_H
#define ZENEDGE_VMM_H

#include <stdint.h>
#include <stddef.h>
#include "pmm.h"

/* Virtual address type */
typedef uint32_t vaddr_t;

/* Higher-half kernel base */
#define KERNEL_VBASE        0xC0000000
#define KERNEL_VBASE_PDE    (KERNEL_VBASE >> 22)  /* PDE index = 768 */

/* Page table constants */
#define PAGE_ENTRIES        1024    /* Entries per table/directory */
#define PDE_INDEX(va)       (((va) >> 22) & 0x3FF)
#define PTE_INDEX(va)       (((va) >> 12) & 0x3FF)
#define PAGE_OFFSET(va)     ((va) & 0xFFF)

/* Page table entry flags (x86) */
#define PTE_PRESENT         (1 << 0)    /* Page is present in memory */
#define PTE_WRITABLE        (1 << 1)    /* Page is writable */
#define PTE_USER            (1 << 2)    /* Page accessible from ring 3 */
#define PTE_WRITE_THROUGH   (1 << 3)    /* Write-through caching */
#define PTE_CACHE_DISABLE   (1 << 4)    /* Disable caching */
#define PTE_ACCESSED        (1 << 5)    /* Page was accessed (set by CPU) */
#define PTE_DIRTY           (1 << 6)    /* Page was written (set by CPU) */
#define PTE_GLOBAL          (1 << 8)    /* Global page (not flushed on CR3 reload) */

/* Common flag combinations */
#define PTE_KERNEL          (PTE_PRESENT | PTE_WRITABLE)
#define PTE_KERNEL_RO       (PTE_PRESENT)
#define PTE_USER_RO         (PTE_PRESENT | PTE_USER)
#define PTE_USER_RW         (PTE_PRESENT | PTE_WRITABLE | PTE_USER)

/* Page directory entry (same structure as PTE for 4KB pages) */
typedef uint32_t pde_t;

/* Page table entry */
typedef uint32_t pte_t;

/* Extract physical address from PDE/PTE */
#define PTE_ADDR(entry)     ((entry) & 0xFFFFF000)

/* Create a PDE/PTE from physical address and flags */
#define MAKE_PTE(paddr, flags)  (((paddr) & 0xFFFFF000) | (flags))

/*
 * Page directory structure
 * Must be page-aligned (4KB boundary)
 */
typedef struct __attribute__((aligned(4096))) {
    pde_t entries[PAGE_ENTRIES];
} page_directory_t;

/*
 * Page table structure
 * Must be page-aligned (4KB boundary)
 */
typedef struct __attribute__((aligned(4096))) {
    pte_t entries[PAGE_ENTRIES];
} page_table_t;

/*
 * Initialize VMM - called after PMM is initialized
 * Sets up kernel page tables and enables paging
 */
void vmm_init(void);

/*
 * Get the current page directory physical address
 */
paddr_t vmm_get_current_pd(void);

/*
 * Switch to a different page directory
 * @param pd_phys: Physical address of new page directory
 */
void vmm_switch_pd(paddr_t pd_phys);

/*
 * Map a virtual address to a physical address
 * @param vaddr: Virtual address to map (will be page-aligned)
 * @param paddr: Physical address to map to (will be page-aligned)
 * @param flags: Page table flags (PTE_*)
 * @return: 0 on success, -1 on failure
 */
int vmm_map_page(vaddr_t vaddr, paddr_t paddr, uint32_t flags);

/*
 * Map a range of virtual addresses to physical addresses
 * @param vaddr: Starting virtual address
 * @param paddr: Starting physical address
 * @param size: Size in bytes (will be rounded up to pages)
 * @param flags: Page table flags
 * @return: 0 on success, -1 on failure
 */
int vmm_map_range(vaddr_t vaddr, paddr_t paddr, uint32_t size, uint32_t flags);

/*
 * Unmap a virtual address
 * @param vaddr: Virtual address to unmap
 * @return: Physical address that was mapped, or 0 if not mapped
 */
paddr_t vmm_unmap_page(vaddr_t vaddr);

/*
 * Get the physical address for a virtual address
 * @param vaddr: Virtual address to translate
 * @return: Physical address, or 0 if not mapped
 */
paddr_t vmm_virt_to_phys(vaddr_t vaddr);

/*
 * Check if a virtual address is mapped
 * @param vaddr: Virtual address to check
 * @return: 1 if mapped, 0 if not
 */
int vmm_is_mapped(vaddr_t vaddr);

/*
 * Invalidate a TLB entry for a virtual address
 * @param vaddr: Virtual address to invalidate
 */
void vmm_invlpg(vaddr_t vaddr);

/*
 * Flush entire TLB (reload CR3)
 */
void vmm_flush_tlb(void);

/*
 * Create a new page directory for a user process
 * Kernel mappings (>= KERNEL_VBASE) are shared
 * @return: Physical address of new page directory, or 0 on failure
 */
paddr_t vmm_create_user_pd(void);

/*
 * Destroy a user page directory and free all user-space pages
 * @param pd_phys: Physical address of page directory to destroy
 */
void vmm_destroy_user_pd(paddr_t pd_phys);

/*
 * Convert physical address to kernel virtual address
 * Only works for addresses within the identity-mapped kernel region
 */
static inline vaddr_t phys_to_virt(paddr_t paddr) {
    return (vaddr_t)(paddr + KERNEL_VBASE);
}

/*
 * Convert kernel virtual address to physical address
 * Only works for addresses within the identity-mapped kernel region
 */
static inline paddr_t virt_to_phys(vaddr_t vaddr) {
    return (paddr_t)(vaddr - KERNEL_VBASE);
}

/*
 * Debug: dump page directory to console
 */
void vmm_dump_pd(paddr_t pd_phys);

#endif /* ZENEDGE_VMM_H */
