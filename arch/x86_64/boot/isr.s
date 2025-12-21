/* kernel/arch/x86_64/isr.s - x86_64 Interrupt Stubs */

.section .text
.code64

/*
 * Interrupt Stack Frame (pushed by CPU on 64-bit long mode interrupt):
 * [RSP + 40] SS
 * [RSP + 32] RSP
 * [RSP + 24] RFLAGS
 * [RSP + 16] CS
 * [RSP + 8]  RIP
 * [RSP + 0]  Error Code (pushed by CPU for some, dummy for others)
 */

/*
 * interrupt_frame_t from idt.h lines up with what we push.
 * We need to save all registers (ABI: System V AMD64 is usually what we follow,
 * but for interrupts we should save everything that might be clobbered).
 */

.extern isr_handler

/* Macro to define an ISR without error code */
.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    pushq $0            /* Dummy error code */
    pushq $\num         /* Interrupt number */
    jmp isr_common_stub
.endm

/* Macro to define an ISR with error code */
.macro ISR_ERRCODE num
.global isr\num
isr\num:
    pushq $\num         /* Interrupt number */
    jmp isr_common_stub
.endm

/* Macro to define an IRQ */
.macro IRQ num, map_num
.global irq\num
irq\num:
    pushq $0            /* Dummy error code */
    pushq $\map_num     /* Remmapped number (32 + num) */
    jmp isr_common_stub
.endm

/* Common ISR Stub */
isr_common_stub:
    /* Save registers */
    /* struct interrupt_frame_t layout:
       ds, edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax, int_no, err_code, rip, cs, rflags, rsp, ss */
    
    /* We need to push registers to match the struct or just push generic context.
       Let's stick to the 32-bit layout logic but expanded for 64-bit.
       Actually, `interrupt_frame_t` in idt.h uses uint32_t which is WRONG for 64-bit.
       Wait, I haven't updated interrupt_frame_t in idt.h yet!
       CRITICAL: I need to update interrupt_frame_t to use uint64_t.
       
       For now, let's assume we update the struct to:
       uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
       uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
       uint64_t int_no, err_code;
       uint64_t rip, cs, rflags, rsp, ss;
    */

    /* Push General Purpose Registers */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Move stack pointer to RDI (first arg) -> &frame */
    movq %rsp, %rdi
    
    /* Alignment check? System V requires 16-byte stack alignment before call.
       We pushed:
         SS, RSP, RFLAGS, CS, RIP (5 * 8 = 40)
         Err, IntNo (2 * 8 = 16) -> 56
         RAX..R15 (15 * 8 = 120) -> 176
       Length is 176 bytes. 176 % 16 == 0. Aligned.
    */

    call isr_handler

    /* Restore registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /* Clean up int_no and error code */
    addq $16, %rsp

    iretq

/* Exceptions */
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

/* Hardware IRQs */
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

/* Syscall */
ISR_NOERRCODE 128

.global idt_flush
idt_flush:
    lidt (%rdi)
    ret
