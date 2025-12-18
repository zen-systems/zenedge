/* kernel/arch/gdt.c - Global Descriptor Table implementation
 *
 * Sets up the GDT with flat segments and TSS for ring transitions.
 */

#include "gdt.h"
#include "../console.h"

/* Number of GDT entries */
#define GDT_ENTRIES 6

/* GDT and TSS are statically allocated */
static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;
static tss_t       tss;

/* External assembly function to load GDT and TSS */
extern void gdt_flush(gdt_ptr_t *ptr);
extern void tss_flush(uint16_t selector);

/* Set a GDT entry */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t granularity) {
    gdt[index].base_low    = base & 0xFFFF;
    gdt[index].base_mid    = (base >> 16) & 0xFF;
    gdt[index].base_high   = (base >> 24) & 0xFF;
    gdt[index].limit_low   = limit & 0xFFFF;
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    gdt[index].access      = access;
}

/* Initialize TSS */
static void tss_init(void) {
    uint32_t tss_base = (uint32_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;

    /* Zero out the TSS */
    uint8_t *p = (uint8_t *)&tss;
    for (uint32_t i = 0; i < sizeof(tss); i++) {
        p[i] = 0;
    }

    /* Set up kernel stack segment and pointer
     * esp0 will be set properly when we have a kernel stack per process
     * For now, use current stack (will be updated by scheduler)
     */
    tss.ss0 = GDT_KERNEL_DATA_SEG;
    tss.esp0 = 0;  /* Will be set by gdt_set_kernel_stack() */

    /* Set I/O map base to end of TSS (no I/O bitmap) */
    tss.iomap_base = sizeof(tss);

    /* Add TSS descriptor to GDT
     * Access: Present | Ring 0 | TSS type (32-bit available)
     * Note: TSS uses byte granularity, not 4KB
     */
    gdt_set_entry(5, tss_base, tss_limit,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_TSS_TYPE_32,
                  GDT_GRAN_32BIT);
}

void gdt_init(void) {
    console_write("[gdt] initializing GDT with TSS...\n");

    /* Set up GDT pointer */
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    /* Entry 0: Null descriptor (required) */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Entry 1: Kernel code segment
     * Base: 0, Limit: 4GB (0xFFFFF with 4KB granularity)
     * Access: Present | Ring 0 | Code/Data | Executable | Readable
     * Granularity: 4KB pages | 32-bit segment
     */
    gdt_set_entry(1, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA |
                  GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
                  GDT_GRAN_4K | GDT_GRAN_32BIT);

    /* Entry 2: Kernel data segment
     * Same as code but not executable
     */
    gdt_set_entry(2, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA |
                  GDT_ACCESS_RW,
                  GDT_GRAN_4K | GDT_GRAN_32BIT);

    /* Entry 3: User code segment
     * Same as kernel code but Ring 3
     */
    gdt_set_entry(3, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA |
                  GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
                  GDT_GRAN_4K | GDT_GRAN_32BIT);

    /* Entry 4: User data segment
     * Same as kernel data but Ring 3
     */
    gdt_set_entry(4, 0, 0xFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA |
                  GDT_ACCESS_RW,
                  GDT_GRAN_4K | GDT_GRAN_32BIT);

    /* Entry 5: TSS */
    tss_init();

    /* Load GDT */
    gdt_flush(&gdt_ptr);

    /* Load TSS selector into task register */
    tss_flush(GDT_TSS_SEG);

    console_write("[gdt] GDT loaded, TSS installed\n");
}

void gdt_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}
