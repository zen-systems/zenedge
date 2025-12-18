/* kernel/arch/switch.s - Context Switching Assembly
 *
 * Handles the low-level transition between processes.
 */

.section .text
.global enter_user_mode
.global switch_to
.extern gdt_set_kernel_stack

/*
 * void enter_user_mode(void *entry_point, void *user_stack)
 * ... (existing implementation) ...
 */
enter_user_mode:
    /* Get arguments from stack */
    mov 4(%esp), %ebx    /* entry_point */
    mov 8(%esp), %ecx    /* user_stack */

    /* Disable interrupts */
    cli

    /* Set up data segments for user mode (RPL=3) */
    mov $0x23, %ax      /* 0x20 (User Data) | 3 (RPL) */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Prepare stack for IRET */
    mov $0x23, %eax     /* SS */
    push %eax
    push %ecx           /* ESP */
    pushf
    pop %eax
    or $0x200, %eax     /* Enable IF */
    push %eax
    mov $0x1B, %eax     /* CS */
    push %eax
    push %ebx           /* EIP */
    iret

/*
 * void switch_to(process_t *curr, process_t *next)
 *
 * Switches execution context from 'curr' process to 'next' process.
 * C Declaration: void switch_to(process_t *curr, process_t *next);
 */
switch_to:
    /* Stack Layout (on entry):
     * [ESP + 8] next (process_t*)
     * [ESP + 4] curr (process_t*)
     * [ESP    ] Return Address
     */

    /* 1. Save Callee-Saved Registers (EBP, EBX, ESI, EDI) */
    push %ebp
    push %ebx
    push %esi
    push %edi

    /* 2. Save current ESP to curr->esp */
    mov 20(%esp), %eax    /* Get 'curr' pointer */
    mov %esp, 4(%eax)     /* Save ESP to curr->esp (offset 4, check process.h!) */

    /* 3. Load next ESP from next->esp */
    mov 24(%esp), %ecx    /* Get 'next' pointer */
    mov 4(%ecx), %esp     /* Load ESP from next->esp */

    /* 4. Switch Page Directory (if different) */
    /* next->cr3 is at offset 8 (pid=4, esp=4) */
    mov 8(%ecx), %edx     /* Get next->cr3 */
    mov %cr3, %eax
    cmp %eax, %edx
    je .no_cr3_switch
    mov %edx, %cr3
.no_cr3_switch:

    /* 5. Update TSS Kernel Stack (next->kstack_top at offset 12) */
    mov 12(%ecx), %eax
    push %ecx             /* Save next pointer (caller-save convention doesn't apply to stack args for C calls usually, but safe to push) */
                          /* Actually we need to pass argument on stack for gdt_set_kernel_stack */
    push %eax
    call gdt_set_kernel_stack
    add $4, %esp
    pop %ecx              /* Restore next pointer if needed (not really needed) */

    /* 6. Restore Callee-Saved Registers */
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp

    /* 7. Return (to the address saved on the new stack) */
    ret
