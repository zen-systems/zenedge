/* kernel/arch/isr.s - Interrupt Service Routine stubs
 *
 * This file contains the low-level assembly stubs for all interrupts.
 * Each stub:
 *   1. Pushes a dummy error code (if CPU didn't push one)
 *   2. Pushes the interrupt number
 *   3. Jumps to the common handler
 *
 * The common handler saves all registers, calls the C handler,
 * restores registers, and returns from interrupt (iret).
 */

.section .text

/* ============================================= */
/* GDT and IDT loading functions                */
/* ============================================= */

.global gdt_flush
.global tss_flush
.global idt_flush

/* void gdt_flush(gdt_ptr_t *ptr)
 * Load the GDT and reload segment registers
 */
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)

    /* Reload segment registers with new selectors */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /* Far jump to reload CS with kernel code segment */
    ljmp $0x08, $.gdt_flush_done
.gdt_flush_done:
    ret

/* void tss_flush(uint16_t selector)
 * Load the Task Register with TSS selector
 */
tss_flush:
    mov 4(%esp), %ax
    ltr %ax
    ret

/* void idt_flush(idt_ptr_t *ptr)
 * Load the IDT
 */
idt_flush:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

/* ============================================= */
/* ISR stubs for CPU exceptions (0-31)          */
/* ============================================= */

/* Macro for ISRs that don't push an error code */
.macro ISR_NOERR num
.global isr\num
isr\num:
    push $0
    push $\num
    jmp isr_common_stub
.endm

/* Macro for ISRs that push an error code */
.macro ISR_ERR num
.global isr\num
isr\num:
    push $\num
    jmp isr_common_stub
.endm

/* CPU Exceptions 0-31 */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

/* ============================================= */
/* IRQ stubs for hardware interrupts (32-47)    */
/* ============================================= */

/* Macro for IRQ stubs */
.macro IRQ irq_num, int_num
.global irq\irq_num
irq\irq_num:
    push $0
    push $\int_num
    jmp irq_common_stub
.endm

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

/* ============================================= */
/* Syscall stub (int 0x80)                      */
/* ============================================= */

.global isr128
isr128:
    push $0
    push $128
    jmp isr_common_stub

/* ============================================= */
/* Common interrupt handler                     */
/* ============================================= */

/* External C handler */
.extern isr_handler

isr_common_stub:
    /* Save all general-purpose registers */
    pusha

    /* Save data segment */
    mov %ds, %ax
    push %eax

    /* Load kernel data segment */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Call C handler with pointer to interrupt_frame_t */
    push %esp
    call isr_handler
    add $4, %esp

    /* Restore data segment */
    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Restore general-purpose registers */
    popa

    /* Clean up error code and interrupt number */
    add $8, %esp

    /* Return from interrupt */
    iret

/* ============================================= */
/* IRQ common handler (with PIC EOI)            */
/* ============================================= */

.extern irq_handler

irq_common_stub:
    /* Save all general-purpose registers */
    pusha

    /* Save data segment */
    mov %ds, %ax
    push %eax

    /* Load kernel data segment */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Call C handler with pointer to interrupt_frame_t */
    push %esp
    call irq_handler
    add $4, %esp

    /* Restore data segment */
    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Restore general-purpose registers */
    popa

    /* Clean up error code and interrupt number */
    add $8, %esp

    /* Return from interrupt */
    iret
