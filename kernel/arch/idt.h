/* kernel/arch/idt.h - Interrupt Descriptor Table
 *
 * The IDT maps interrupt vectors (0-255) to handler functions.
 *
 * Interrupt layout:
 *   0-31:   CPU exceptions (divide error, page fault, etc.)
 *   32-47:  Hardware IRQs (remapped from 0-15)
 *   48-255: Software interrupts (syscall at 0x80 = 128)
 */

#ifndef _ARCH_IDT_H
#define _ARCH_IDT_H

#include <stdint.h>

/* Number of IDT entries */
#define IDT_ENTRIES 256

/* CPU exception vectors */
#define INT_DIVIDE_ERROR        0   /* #DE - Divide by zero */
#define INT_DEBUG               1   /* #DB - Debug */
#define INT_NMI                 2   /* NMI - Non-maskable interrupt */
#define INT_BREAKPOINT          3   /* #BP - Breakpoint (INT 3) */
#define INT_OVERFLOW            4   /* #OF - Overflow (INTO) */
#define INT_BOUND_EXCEEDED      5   /* #BR - BOUND range exceeded */
#define INT_INVALID_OPCODE      6   /* #UD - Invalid opcode */
#define INT_DEVICE_NOT_AVAIL    7   /* #NM - Device not available (FPU) */
#define INT_DOUBLE_FAULT        8   /* #DF - Double fault */
#define INT_COPROC_OVERRUN      9   /* Coprocessor segment overrun (obsolete) */
#define INT_INVALID_TSS        10   /* #TS - Invalid TSS */
#define INT_SEGMENT_NOT_PRES   11   /* #NP - Segment not present */
#define INT_STACK_FAULT        12   /* #SS - Stack-segment fault */
#define INT_GENERAL_PROT       13   /* #GP - General protection fault */
#define INT_PAGE_FAULT         14   /* #PF - Page fault */
#define INT_RESERVED_15        15   /* Reserved */
#define INT_X87_FPU_ERROR      16   /* #MF - x87 FPU error */
#define INT_ALIGNMENT_CHECK    17   /* #AC - Alignment check */
#define INT_MACHINE_CHECK      18   /* #MC - Machine check */
#define INT_SIMD_FP_ERROR      19   /* #XM - SIMD floating-point exception */
#define INT_VIRT_ERROR         20   /* #VE - Virtualization exception */
#define INT_CONTROL_PROT       21   /* #CP - Control protection exception */
/* 22-31: Reserved */

/* Hardware IRQ vectors (after PIC remapping) */
#define IRQ_BASE               32   /* IRQs start at vector 32 */
#define IRQ0_TIMER             (IRQ_BASE + 0)   /* PIT timer */
#define IRQ1_KEYBOARD          (IRQ_BASE + 1)   /* Keyboard */
#define IRQ2_CASCADE           (IRQ_BASE + 2)   /* Cascade (for slave PIC) */
#define IRQ3_COM2              (IRQ_BASE + 3)   /* COM2 */
#define IRQ4_COM1              (IRQ_BASE + 4)   /* COM1 */
#define IRQ5_LPT2              (IRQ_BASE + 5)   /* LPT2 */
#define IRQ6_FLOPPY            (IRQ_BASE + 6)   /* Floppy */
#define IRQ7_LPT1              (IRQ_BASE + 7)   /* LPT1 / Spurious */
#define IRQ8_RTC               (IRQ_BASE + 8)   /* RTC */
#define IRQ9_FREE              (IRQ_BASE + 9)   /* Free */
#define IRQ10_FREE             (IRQ_BASE + 10)  /* Free */
#define IRQ11_FREE             (IRQ_BASE + 11)  /* Free */
#define IRQ12_MOUSE            (IRQ_BASE + 12)  /* PS/2 Mouse */
#define IRQ13_FPU              (IRQ_BASE + 13)  /* FPU */
#define IRQ14_ATA_PRI          (IRQ_BASE + 14)  /* ATA Primary */
#define IRQ15_ATA_SEC          (IRQ_BASE + 15)  /* ATA Secondary */

/* Software interrupt vectors */
#define INT_SYSCALL            0x80  /* System call (128) */

/* IDT entry structure (8 bytes for 32-bit, 16 bytes for 64-bit) */
#if defined(__x86_64__)
typedef struct __attribute__((packed)) {
    uint16_t offset_low;     /* Offset 0-15 */
    uint16_t selector;       /* Segment Selector */
    uint8_t  ist;            /* Interrupt Stack Table (0-7) | Reserved */
    uint8_t  type_attr;      /* Type and Attributes */
    uint16_t offset_mid;     /* Offset 16-31 */
    uint32_t offset_high;    /* Offset 32-63 */
    uint32_t reserved;       /* Reserved */
} idt_entry_t;

/* IDT pointer for LIDT instruction (10 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t limit;          /* Size of IDT - 1 */
    uint64_t base;           /* Linear address of IDT */
} idt_ptr_t;
#else
typedef struct __attribute__((packed)) {
    uint16_t offset_low;     /* Handler offset bits 0-15 */
    uint16_t selector;       /* Code segment selector */
    uint8_t  zero;           /* Reserved, must be 0 */
    uint8_t  type_attr;      /* Type and attributes */
    uint16_t offset_high;    /* Handler offset bits 16-31 */
} idt_entry_t;

/* IDT pointer for LIDT instruction */
typedef struct __attribute__((packed)) {
    uint16_t limit;          /* Size of IDT - 1 */
    uint32_t base;           /* Linear address of IDT */
} idt_ptr_t;
#endif

/* Gate types for type_attr field */
#define IDT_GATE_TASK          0x05  /* Task gate (unused) */
#define IDT_GATE_INT16         0x06  /* 16-bit interrupt gate */
#define IDT_GATE_TRAP16        0x07  /* 16-bit trap gate */
#define IDT_GATE_INT32         0x0E  /* 32-bit interrupt gate */
#define IDT_GATE_TRAP32        0x0F  /* 32-bit trap gate */

/* Type attribute flags */
#define IDT_ATTR_PRESENT       (1 << 7)  /* Present bit */
#define IDT_ATTR_RING0         (0 << 5)  /* Ring 0 (kernel) */
#define IDT_ATTR_RING3         (3 << 5)  /* Ring 3 (user) - for syscalls */

/* Interrupt stack frame pushed by CPU */
/* Interrupt stack frame pushed by CPU */
#if defined(__x86_64__)
typedef struct __attribute__((packed)) {
    /* Pushed by our ISR stub */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;         /* Interrupt number */
    uint64_t err_code;       /* Error code */
    /* Pushed by CPU */
    uint64_t rip;            /* Instruction pointer */
    uint64_t cs;             /* Code segment */
    uint64_t rflags;         /* Flags register */
    uint64_t rsp;            /* Stack pointer */
    uint64_t ss;             /* Stack segment */
} interrupt_frame_t;
#else
typedef struct __attribute__((packed)) {
    /* Pushed by our ISR stub */
    uint32_t ds;             /* Data segment selector */
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;  /* pusha */
    uint32_t int_no;         /* Interrupt number */
    uint32_t err_code;       /* Error code (or dummy) */
    /* Pushed by CPU automatically */
    uint32_t eip;            /* Instruction pointer */
    uint32_t cs;             /* Code segment */
    uint32_t eflags;         /* Flags register */
    /* Pushed only on privilege level change (user -> kernel) */
    uint32_t user_esp;       /* User stack pointer */
    uint32_t user_ss;        /* User stack segment */
} interrupt_frame_t;
#endif

/* Initialize IDT and install handlers */
void idt_init(void);

/* Register a custom interrupt handler */
typedef void (*interrupt_handler_t)(interrupt_frame_t *frame);
void idt_register_handler(uint8_t vector, interrupt_handler_t handler);

/* Enable/disable interrupts */
static inline void interrupts_enable(void) {
    __asm__ __volatile__("sti");
}

static inline void interrupts_disable(void) {
    __asm__ __volatile__("cli");
}

static inline int interrupts_enabled(void) {
    uint32_t flags;
    __asm__ __volatile__("pushf; pop %0" : "=r"(flags));
    return (flags & 0x200) != 0;  /* IF flag is bit 9 */
}

#endif /* _ARCH_IDT_H */
