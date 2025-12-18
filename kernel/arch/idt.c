/* kernel/arch/idt.c - Interrupt Descriptor Table implementation
 *
 * Sets up the IDT and provides the common interrupt dispatcher.
 */

#include "idt.h"
#include "gdt.h"
#include "../console.h"

/* IDT storage */
static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;

/* Custom handlers registered by kernel subsystems */
static interrupt_handler_t handlers[IDT_ENTRIES];

/* External assembly function to load IDT */
extern void idt_flush(idt_ptr_t *ptr);

/* External ISR stubs (defined in isr.s) */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* IRQ stubs */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/* Syscall stub */
extern void isr128(void);

/* Set an IDT entry */
static void idt_set_entry(int index, uint32_t handler, uint16_t selector,
                          uint8_t type_attr) {
    idt[index].offset_low  = handler & 0xFFFF;
    idt[index].offset_high = (handler >> 16) & 0xFFFF;
    idt[index].selector    = selector;
    idt[index].zero        = 0;
    idt[index].type_attr   = type_attr;
}

/* Exception names for debugging */
static const char *exception_names[] = {
    "Divide Error",           /* 0 */
    "Debug",                  /* 1 */
    "NMI",                    /* 2 */
    "Breakpoint",             /* 3 */
    "Overflow",               /* 4 */
    "BOUND Exceeded",         /* 5 */
    "Invalid Opcode",         /* 6 */
    "Device Not Available",   /* 7 */
    "Double Fault",           /* 8 */
    "Coprocessor Overrun",    /* 9 */
    "Invalid TSS",            /* 10 */
    "Segment Not Present",    /* 11 */
    "Stack Fault",            /* 12 */
    "General Protection",     /* 13 */
    "Page Fault",             /* 14 */
    "Reserved",               /* 15 */
    "x87 FPU Error",          /* 16 */
    "Alignment Check",        /* 17 */
    "Machine Check",          /* 18 */
    "SIMD FP Error",          /* 19 */
    "Virtualization Error",   /* 20 */
    "Control Protection",     /* 21 */
};

/* Print hex value helper */
static void print_hex(uint32_t val) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(val >> (28 - i * 4)) & 0xF];
    }
    buf[10] = '\0';
    console_write(buf);
}

/* Default exception handler - panic */
static void default_exception_handler(interrupt_frame_t *frame) {
    console_write("\n\n*** KERNEL PANIC: ");

    if (frame->int_no < 22) {
        console_write(exception_names[frame->int_no]);
    } else {
        console_write("Unknown Exception");
    }
    console_write(" ***\n");

    console_write("  Vector: ");
    print_hex(frame->int_no);
    console_write("  Error: ");
    print_hex(frame->err_code);
    console_write("\n");

    console_write("  EIP: ");
    print_hex(frame->eip);
    console_write("  CS: ");
    print_hex(frame->cs);
    console_write("  EFLAGS: ");
    print_hex(frame->eflags);
    console_write("\n");

    console_write("  EAX: ");
    print_hex(frame->eax);
    console_write("  EBX: ");
    print_hex(frame->ebx);
    console_write("  ECX: ");
    print_hex(frame->ecx);
    console_write("  EDX: ");
    print_hex(frame->edx);
    console_write("\n");

    console_write("  ESI: ");
    print_hex(frame->esi);
    console_write("  EDI: ");
    print_hex(frame->edi);
    console_write("  EBP: ");
    print_hex(frame->ebp);
    console_write("\n");

    /* For page faults, print CR2 (faulting address) */
    if (frame->int_no == INT_PAGE_FAULT) {
        uint32_t cr2;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
        console_write("  CR2 (fault addr): ");
        print_hex(cr2);
        console_write("\n");

        /* Decode page fault error code */
        console_write("  Fault: ");
        if (frame->err_code & 0x1) console_write("protection ");
        else console_write("not-present ");
        if (frame->err_code & 0x2) console_write("write ");
        else console_write("read ");
        if (frame->err_code & 0x4) console_write("user ");
        else console_write("kernel ");
        console_write("\n");
    }

    /* Halt */
    console_write("\nSystem halted.\n");
    interrupts_disable();
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

/* Common interrupt dispatcher - called from assembly stubs */
void isr_handler(interrupt_frame_t *frame) {
    /* Call registered handler if present */
    if (handlers[frame->int_no]) {
        handlers[frame->int_no](frame);
    } else if (frame->int_no < 32) {
        /* Unhandled CPU exception - panic */
        default_exception_handler(frame);
    }
    /* Unhandled IRQs/software interrupts are silently ignored */
}

void idt_register_handler(uint8_t vector, interrupt_handler_t handler) {
    handlers[vector] = handler;
}

void idt_init(void) {
    console_write("[idt] initializing IDT...\n");

    /* Set up IDT pointer */
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    /* Clear handlers array */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        handlers[i] = 0;
    }

    /* CPU exceptions (0-31) - interrupt gates (interrupts disabled) */
    uint8_t exc_attr = IDT_ATTR_PRESENT | IDT_ATTR_RING0 | IDT_GATE_INT32;

    idt_set_entry(0,  (uint32_t)isr0,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(1,  (uint32_t)isr1,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(2,  (uint32_t)isr2,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(3,  (uint32_t)isr3,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(4,  (uint32_t)isr4,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(5,  (uint32_t)isr5,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(6,  (uint32_t)isr6,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(7,  (uint32_t)isr7,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(8,  (uint32_t)isr8,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(9,  (uint32_t)isr9,  GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(10, (uint32_t)isr10, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(11, (uint32_t)isr11, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(12, (uint32_t)isr12, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(13, (uint32_t)isr13, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(14, (uint32_t)isr14, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(15, (uint32_t)isr15, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(16, (uint32_t)isr16, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(17, (uint32_t)isr17, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(18, (uint32_t)isr18, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(19, (uint32_t)isr19, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(20, (uint32_t)isr20, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(21, (uint32_t)isr21, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(22, (uint32_t)isr22, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(23, (uint32_t)isr23, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(24, (uint32_t)isr24, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(25, (uint32_t)isr25, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(26, (uint32_t)isr26, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(27, (uint32_t)isr27, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(28, (uint32_t)isr28, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(29, (uint32_t)isr29, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(30, (uint32_t)isr30, GDT_KERNEL_CODE_SEG, exc_attr);
    idt_set_entry(31, (uint32_t)isr31, GDT_KERNEL_CODE_SEG, exc_attr);

    /* Hardware IRQs (32-47) */
    uint8_t irq_attr = IDT_ATTR_PRESENT | IDT_ATTR_RING0 | IDT_GATE_INT32;

    idt_set_entry(32, (uint32_t)irq0,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(33, (uint32_t)irq1,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(34, (uint32_t)irq2,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(35, (uint32_t)irq3,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(36, (uint32_t)irq4,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(37, (uint32_t)irq5,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(38, (uint32_t)irq6,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(39, (uint32_t)irq7,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(40, (uint32_t)irq8,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(41, (uint32_t)irq9,  GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(42, (uint32_t)irq10, GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(43, (uint32_t)irq11, GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(44, (uint32_t)irq12, GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(45, (uint32_t)irq13, GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(46, (uint32_t)irq14, GDT_KERNEL_CODE_SEG, irq_attr);
    idt_set_entry(47, (uint32_t)irq15, GDT_KERNEL_CODE_SEG, irq_attr);

    /* Syscall interrupt (0x80 = 128) - trap gate, accessible from ring 3 */
    uint8_t syscall_attr = IDT_ATTR_PRESENT | IDT_ATTR_RING3 | IDT_GATE_TRAP32;
    idt_set_entry(INT_SYSCALL, (uint32_t)isr128, GDT_KERNEL_CODE_SEG, syscall_attr);

    /* Load IDT */
    idt_flush(&idt_ptr);

    console_write("[idt] IDT loaded with 32 exceptions + 16 IRQs + syscall\n");
}
