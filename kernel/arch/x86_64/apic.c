/* kernel/arch/x86_64/apic.c - Local APIC Implementation */

#include "../apic.h"
#include "../../console.h"
#include "../../mm/vmm.h"

/* Default LAPIC physical address is 0xFEE00000 */
/* We need to map this to virtual memory */
#define LAPIC_PHYS_BASE 0xFEE00000
#define LAPIC_VIRT_BASE 0xFFFFFFFFEE000000 /* Map at -288MB? Or just 1:1 if identity mapped? */
/* Actually, let's use a safe high hook. 0xFEE00000 is usually reserved in memory map. */

static volatile uint32_t *lapic_base = (volatile uint32_t *)LAPIC_PHYS_BASE;

/* Helper: Read MSR */
static uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

/* Helper: Write MSR */
static void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = val >> 32;
    __asm__ __volatile__("wrmsr" :: "a"(low), "d"(high), "c"(msr));
}

uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t *)((uint8_t *)lapic_base + reg);
}

void lapic_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t *)((uint8_t *)lapic_base + reg) = value;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

uint32_t lapic_get_id(void) {
    return (lapic_read(LAPIC_ID) >> 24) & 0xFF;
}

void lapic_init(void) {
    console_write("[apic] initializing Local APIC...\n");

    /* check MSR for APIC base */
    uint64_t apic_msr = rdmsr(IA32_APIC_BASE_MSR);
    uint32_t phys_base = apic_msr & 0xFFFFF000;
    
    /* Map if necessary. existing paging might cover it if identity mapped.
       But for safety, we should map it.
       Wait, our VMM (vmm.c) maps standard high kernel.
       We should probably identity map this page.
    */
    /* vmm_map_page(LAPIC_PHYS_BASE, LAPIC_PHYS_BASE, PTE_PRESENT | PTE_RW | PTE_CACHE_DISABLE); */
    
    lapic_base = (volatile uint32_t *)(uintptr_t)phys_base;
    
    console_write("[apic] Base: ");
    print_hex32(phys_base);
    console_write("\n");

    /* Enable APIC globally */
    wrmsr(IA32_APIC_BASE_MSR, apic_msr | IA32_APIC_BASE_MSR_ENABLE);

    /* Set Spurious Interrupt Vector (bit 8 = enable) */
    /* Vector 0xFF (255) is commonly used for spurious */
    lapic_write(LAPIC_SVR, 0xFF | APIC_SVR_ENABLE);
    
    console_write("[apic] enabled. ID=");
    print_uint(lapic_get_id());
    console_write("\n");
}
