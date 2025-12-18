/* kernel/mm/vmm.c
 *
 * Virtual Memory Manager implementation for ZENEDGE
 *
 * Key insight: The boot code (start.s) sets up initial page tables and
 * enables paging BEFORE calling kmain. By the time we get here, paging
 * is already enabled and the kernel is running at virtual addresses.
 *
 * Initial mapping done by boot code:
 * - Identity maps first 4MB (for boot transition)
 * - Maps kernel at 0xC0000000+ to physical 0x100000+
 *
 * This file manages page tables after paging is enabled.
 */
#include "vmm.h"
#include "pmm.h"
#include "../console.h"
#include "../trace/flightrec.h"

/* Kernel page directory - set up by boot code, refined here */
extern page_directory_t boot_page_directory;
extern page_table_t boot_page_table_identity;
extern page_table_t boot_page_table_kernel;

/* Current page directory physical address */
static paddr_t current_pd_phys = 0;

/* Read CR3 register */
static inline paddr_t read_cr3(void) {
    paddr_t val;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(val));
    return val;
}

/* Write CR3 register */
static inline void write_cr3(paddr_t val) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(val) : "memory");
}

/* Invalidate a single TLB entry */
void vmm_invlpg(vaddr_t vaddr) {
    __asm__ __volatile__("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/* Flush entire TLB */
void vmm_flush_tlb(void) {
    write_cr3(read_cr3());
}

/*
 * Get a virtual address for accessing a page table
 * Since we have a simple identity-map for kernel memory,
 * we just add KERNEL_VBASE to the physical address
 */
static inline void *pd_entry_to_virt(paddr_t paddr) {
    return (void *)phys_to_virt(paddr);
}

void vmm_init(void) {
    console_write("[vmm] initializing virtual memory manager\n");

    /* Get current page directory from CR3 */
    current_pd_phys = read_cr3() & 0xFFFFF000;
    page_directory_t *pd = (page_directory_t *)phys_to_virt(current_pd_phys);

    console_write("[vmm] current page directory at ");
    print_hex32(current_pd_phys);
    console_write("\n");

    /* 
     * Explicitly map 128MB of kernel memory (0xC0000000 - 0xC8000000)
     * using 4MB PSE pages, matching the bootloader's work.
     * Use entries 768-799 (32 entries * 4MB = 128MB).
     */
    for (int i = 0; i < 32; i++) {
        paddr_t paddr = i * 0x400000; // 4MB increments
        pd->entries[KERNEL_VBASE_PDE + i] = MAKE_PTE(paddr, PTE_PRESENT | PTE_WRITABLE | PTE_PSE | PTE_GLOBAL);
    }

    console_write("[vmm] paging enabled, kernel mapped 128MB@0xC0000000 (PSE)\n");

    flightrec_log(TRACE_EVT_BOOT, 0, 0, 0xC0DE);  /* VMM initialized marker */
    console_write("[vmm] init complete\n");
}

paddr_t vmm_get_current_pd(void) {
    return current_pd_phys;
}

/* Serial port output for debugging (survives crashes) */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void serial_char(char c) { outb(0x3F8, c); }

void vmm_switch_pd(paddr_t pd_phys) {
    serial_char('S');  /* Starting switch */
    serial_char('W');

    current_pd_phys = pd_phys;

    serial_char('C');  /* About to write CR3 */
    serial_char('R');
    serial_char('3');

    write_cr3(pd_phys);

    serial_char('O');  /* CR3 write succeeded */
    serial_char('K');
    serial_char('\n');
}

/*
 * Map a single page
 * Allocates a page table if needed
 */
/*
 * Map a single page
 * Allocates a page table if needed
 */
int vmm_map_page(vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
    uint32_t pde_idx = PDE_INDEX(vaddr);
    uint32_t pte_idx = PTE_INDEX(vaddr);

    /* Get current page directory from HW to ensure we use the active one */
    paddr_t active_pd_phys = read_cr3() & 0xFFFFF000;
    page_directory_t *pd = (page_directory_t *)phys_to_virt(active_pd_phys);

    /* Check if page table exists */
    if (!(pd->entries[pde_idx] & PTE_PRESENT)) {
        /* Need to allocate a new page table */
        paddr_t pt_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
        if (pt_phys == 0) {
            console_write("[vmm] ERROR: failed to allocate page table\n");
            return -1;
        }

        /* Zero out the new page table */
        page_table_t *pt = (page_table_t *)phys_to_virt(pt_phys);
        for (int i = 0; i < PAGE_ENTRIES; i++) {
            pt->entries[i] = 0;
        }

        /* Install the page table in the directory
         * Page tables inherit user/write permissions from their entries */
        pd->entries[pde_idx] = MAKE_PTE(pt_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    /* Get the page table */
    paddr_t pt_phys = PTE_ADDR(pd->entries[pde_idx]);
    page_table_t *pt = (page_table_t *)phys_to_virt(pt_phys);

    /* Check if already mapped */
    if (pt->entries[pte_idx] & PTE_PRESENT) {
        /* Already mapped - check if it's the same mapping */
        if (PTE_ADDR(pt->entries[pte_idx]) == (paddr & 0xFFFFF000)) {
            /* Same physical address, just update flags */
            pt->entries[pte_idx] = MAKE_PTE(paddr, flags);
            vmm_invlpg(vaddr);
            return 0;
        }
        console_write("[vmm] WARNING: remapping ");
        print_hex32(vaddr);
        console_write("\n");
    }

    /* Create the mapping */
    pt->entries[pte_idx] = MAKE_PTE(paddr, flags);
    vmm_invlpg(vaddr);

    return 0;
}

int vmm_map_range(vaddr_t vaddr, paddr_t paddr, uint32_t size, uint32_t flags) {
    vaddr_t va = vaddr & ~(PAGE_SIZE - 1);
    paddr_t pa = paddr & ~(PAGE_SIZE - 1);
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t i = 0; i < pages; i++) {
        if (vmm_map_page(va, pa, flags) != 0) {
            return -1;
        }
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }

    return 0;
}

paddr_t vmm_unmap_page(vaddr_t vaddr) {
    uint32_t pde_idx = PDE_INDEX(vaddr);
    uint32_t pte_idx = PTE_INDEX(vaddr);

    paddr_t active_pd_phys = read_cr3() & 0xFFFFF000;
    page_directory_t *pd = (page_directory_t *)phys_to_virt(active_pd_phys);

    if (!(pd->entries[pde_idx] & PTE_PRESENT)) {
        return 0;  /* Page table doesn't exist */
    }

    paddr_t pt_phys = PTE_ADDR(pd->entries[pde_idx]);
    page_table_t *pt = (page_table_t *)phys_to_virt(pt_phys);

    if (!(pt->entries[pte_idx] & PTE_PRESENT)) {
        return 0;  /* Page not mapped */
    }

    paddr_t old_paddr = PTE_ADDR(pt->entries[pte_idx]);
    pt->entries[pte_idx] = 0;
    vmm_invlpg(vaddr);

    return old_paddr;
}

paddr_t vmm_virt_to_phys(vaddr_t vaddr) {
    uint32_t pde_idx = PDE_INDEX(vaddr);
    uint32_t pte_idx = PTE_INDEX(vaddr);

    paddr_t active_pd_phys = read_cr3() & 0xFFFFF000;
    page_directory_t *pd = (page_directory_t *)phys_to_virt(active_pd_phys);

    if (!(pd->entries[pde_idx] & PTE_PRESENT)) {
        return 0;
    }

    paddr_t pt_phys = PTE_ADDR(pd->entries[pde_idx]);
    page_table_t *pt = (page_table_t *)phys_to_virt(pt_phys);

    if (!(pt->entries[pte_idx] & PTE_PRESENT)) {
        return 0;
    }

    return PTE_ADDR(pt->entries[pte_idx]) | PAGE_OFFSET(vaddr);
}

int vmm_is_mapped(vaddr_t vaddr) {
    return vmm_virt_to_phys(vaddr) != 0;
}

paddr_t vmm_create_user_pd(void) {
    /* Allocate a page for the new directory */
    paddr_t pd_phys = pmm_alloc_page(NUMA_NODE_LOCAL);
    if (pd_phys == 0) {
        return 0;
    }

    console_write("[vmm] new user PD at phys ");
    print_hex32(pd_phys);
    console_write(" (virt ");
    print_hex32(phys_to_virt(pd_phys));
    console_write(")\n");

    page_directory_t *new_pd = (page_directory_t *)phys_to_virt(pd_phys);
    page_directory_t *kernel_pd = (page_directory_t *)phys_to_virt(current_pd_phys);

    console_write("[vmm] kernel PD at phys ");
    print_hex32(current_pd_phys);
    console_write(" (virt ");
    print_hex32((uint32_t)kernel_pd);
    console_write(")\n");

    /* Clear user space entries (except PDE[0] which we copy for VGA access) */
    for (uint32_t i = 1; i < KERNEL_VBASE_PDE; i++) {
        new_pd->entries[i] = 0;
    }

    /* Copy PDE[0] - identity map for first 4MB (needed for VGA memory at 0xB8000)
     * This is a PSE 4MB page set up by boot code
     */
    new_pd->entries[0] = kernel_pd->entries[0];

    /* Share kernel space entries (PDE 768+) - these are 4MB PSE pages */
    for (uint32_t i = KERNEL_VBASE_PDE; i < PAGE_ENTRIES; i++) {
        new_pd->entries[i] = kernel_pd->entries[i];
    }

    /* Debug: print PDE[768] to verify kernel mapping is copied */
    console_write("[vmm] kernel_pd[768] = ");
    print_hex32(kernel_pd->entries[KERNEL_VBASE_PDE]);
    console_write(", new_pd[768] = ");
    print_hex32(new_pd->entries[KERNEL_VBASE_PDE]);
    console_write("\n");

    return pd_phys;
}

void vmm_destroy_user_pd(paddr_t pd_phys) {
    page_directory_t *pd = (page_directory_t *)phys_to_virt(pd_phys);

    /* Free user-space page tables and pages */
    for (uint32_t pde_idx = 0; pde_idx < KERNEL_VBASE_PDE; pde_idx++) {
        if (pd->entries[pde_idx] & PTE_PRESENT) {
            /* Skip large pages (PSE) - we don't treat them as page tables */
            if (pd->entries[pde_idx] & PTE_PSE) {
                continue;
            }

            paddr_t pt_phys = PTE_ADDR(pd->entries[pde_idx]);
            page_table_t *pt = (page_table_t *)phys_to_virt(pt_phys);

            /* Free all mapped pages */
            for (uint32_t pte_idx = 0; pte_idx < PAGE_ENTRIES; pte_idx++) {
                if (pt->entries[pte_idx] & PTE_PRESENT) {
                    pmm_free_page(PTE_ADDR(pt->entries[pte_idx]));
                }
            }

            /* Free the page table */
            pmm_free_page(pt_phys);
        }
    }

    /* Free the page directory */
    pmm_free_page(pd_phys);
}

void vmm_dump_pd(paddr_t pd_phys) {
    page_directory_t *pd = (page_directory_t *)phys_to_virt(pd_phys);

    console_write("\n=== PAGE DIRECTORY DUMP ===\n");
    console_write("PD at ");
    print_hex32(pd_phys);
    console_write("\n");

    uint32_t mapped_count = 0;
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        if (pd->entries[i] & PTE_PRESENT) {
            console_write("PDE[");
            print_uint(i);
            console_write("] -> ");
            print_hex32(PTE_ADDR(pd->entries[i]));
            console_write(" (vaddr: ");
            print_hex32(i << 22);
            console_write(" - ");
            print_hex32(((i + 1) << 22) - 1);
            console_write(")\n");
            mapped_count++;
        }
    }

    console_write("Total PDEs present: ");
    print_uint(mapped_count);
    console_write("\n=== END DUMP ===\n");
}
