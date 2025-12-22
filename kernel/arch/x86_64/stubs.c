#include "../../console.h"
#include "../../mm/vmm.h"
#include <stdint.h>

/* Stubs for architecture-specific initialization called by kmain.c */

void gdt_init(void) { console_write("[arch] GDT stub\n"); }

void idt_init(void) { console_write("[arch] IDT stub\n"); }

void pic_init(void) { console_write("[arch] PIC stub\n"); }

void pit_init(uint32_t freq) {
  (void)freq;
  console_write("[arch] PIT stub\n");
}

void keyboard_init(void) { console_write("[arch] Keyboard stub\n"); }

void vmm_init(void) { console_write("[mm] VMM stub\n"); }

/* VMM Stubs for IPC/IVSHMEM (Assuming identity map) */
int vmm_map_page(vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
  (void)vaddr;
  (void)paddr;
  (void)flags;
  /* console_write("[mm] vmm_map_page stub\n"); */
  return 0;
}

int vmm_map_range(vaddr_t v, paddr_t p, uint32_t len, uint32_t flags) {
  (void)v;
  (void)p;
  (void)len;
  (void)flags;
  return 0;
}

paddr_t vmm_virt_to_phys(vaddr_t v) { return (paddr_t)v; }

/* Interrupt Stubs */
void isr_handler(void *tf) {
  (void)tf;
  console_write("[arch] ISR stub hit!\n");
}

void irq_register_handler(int irq, void (*handler)(void *)) {
  (void)irq;
  (void)handler;
  /* console_write("[arch] irq_register_handler stub\n"); */
}

void idt_register_handler(int vector, void (*handler)(void *)) {
  (void)vector;
  (void)handler;
}

void pic_unmask_irq(int irq) { (void)irq; }
