/* arch/x86_64/boot/start.s - Minimal Multiboot2 long-mode bring-up (M0) */

.set CR0_PG,   0x80000000
.set CR4_PAE,  0x00000020
.set EFER_MSR, 0xC0000080
.set EFER_LME, 0x00000100

.set PTE_P,    0x001
.set PTE_W,    0x002
.set PDE_PS,   0x080

.section .bss.boot
.align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd:
    .skip 4096
pd1:
    .skip 4096
pd2:
    .skip 4096
pd3:
    .skip 4096

.align 16
boot_stack_bottom:
    .skip 16384
boot_stack_top:

.section .text.boot
.code32
.global start
start:
    cli

    /* Save multiboot2 registers before we touch them:
     *   eax = magic (0x36d76289)
     *   ebx = multiboot2 info pointer (physical)
     */
    mov %eax, mb2_magic
    mov %ebx, mb2_info

    /* Temporary stack (identity-mapped) */
    mov $boot_stack_top, %esp
    and $0xFFFFFFF0, %esp

    /* Clear page tables (3 pages) */
    xor %eax, %eax
    mov $pml4, %edi
    mov $(4096 * 3 / 4), %ecx
    rep stosl

    /* pml4[0] -> pdpt */
    mov $pdpt, %eax
    or $(PTE_P | PTE_W), %eax
    mov %eax, pml4
    movl $0, pml4+4

    /* pml4[511] -> pdpt (higher-half window) */
    mov $pdpt, %eax
    or $(PTE_P | PTE_W), %eax
    mov %eax, pml4+0xFF8
    movl $0, pml4+0xFFC

    /* pdpt[0] -> pd */
    mov $pd, %eax
    or $(PTE_P | PTE_W), %eax
    mov %eax, pdpt
    movl $0, pdpt+4

    /* pdpt[510] -> pd (maps KERN_BASE = 0xFFFFFFFF80000000) */
    mov $pd, %eax
    or $(PTE_P | PTE_W), %eax
    mov %eax, pdpt+0xFF0
    movl $0, pdpt+0xFF4

    /* pdpt[1] -> pd1 (1GB-2GB) */
    mov $pd1, %eax
    or $(PTE_P | PTE_W), %eax
    mov %eax, pdpt+8
    movl $0, pdpt+12

    /* pdpt[2] -> pd2 (2GB-3GB) */
    mov $pd2, %eax
    or $(PTE_P | PTE_W), %eax
    mov %eax, pdpt+16
    movl $0, pdpt+20

    /* pdpt[3] -> pd3 (3GB-4GB) */
    mov $pd3, %eax
    or $(PTE_P | PTE_W), %eax
    mov %eax, pdpt+24
    movl $0, pdpt+28

    /* Identity-map 0..4GiB using 2MiB pages */
    /* We assume pd, pd1, pd2, pd3 are contiguous in BSS (aligned) */
    /* Each PD is 4096 bytes. Total 4 * 4096 = 16384 bytes */
    /* Total 2048 entries */
    
    mov $pd, %edi
    xor %eax, %eax                  /* base physical 0 */
    mov $2048, %ecx                 /* 2048 * 2MiB = 4GiB */
1:
    mov %eax, %edx
    or $(PTE_P | PTE_W | PDE_PS), %edx
    mov %edx, (%edi)                /* low dword */
    movl $0, 4(%edi)                /* high dword */
    add $8, %edi
    add $0x200000, %eax
    dec %ecx
    jnz 1b

    /* Enable PAE */
    mov %cr4, %eax
    or $CR4_PAE, %eax
    mov %eax, %cr4

    /* Load PML4 into CR3 */
    mov $pml4, %eax
    mov %eax, %cr3

    /* Enable long mode (EFER.LME) */
    mov $EFER_MSR, %ecx
    rdmsr
    or $EFER_LME, %eax
    wrmsr

    /* Load 64-bit GDT */
    lgdt gdt64_ptr

    /* Enable paging (enters long mode on far jump) */
    mov %cr0, %eax
    or $CR0_PG, %eax
    mov %eax, %cr0

    /* Far jump to 64-bit code segment */
    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    /* Set data segments */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    /* 16-byte align stack; keep SysV ABI alignment before call */
    mov $boot_stack_top, %rsp
    and $-16, %rsp
    /* sub $8, %rsp <-- REMOVED */

    /* Prepare args: kmain64(uint32_t magic, uint32_t info_ptr) */
    mov mb2_magic(%rip), %edi
    mov mb2_info(%rip), %esi

    /* Jump to higher-half entrypoint (linked at KERN_BASE + phys) */
    movabs $higher_half_entry, %rax
    jmp *%rax

.section .text
.code64
higher_half_entry:
    /* Switch to a higher-half stack */
    mov $kernel_stack_top, %rsp
    and $-16, %rsp
    /* sub $8, %rsp  <-- REMOVED: Breaks ABI alignment (rsp should be 16-byte aligned BEFORE call) */
    
    /* Enable SSE (Required for x86_64 GCC generated code) */
    mov %cr0, %rax
    and $0xFFFB, %ax  /* Clear EM (bit 2) */
    or  $0x0002, %ax  /* Set MP (bit 1) */
    mov %rax, %cr0
    
    mov %cr4, %rax
    or  $0x0600, %rax /* Set OSFXSR (bit 9) and OSXMMEXCPT (bit 10) */
    mov %rax, %cr4

    .extern kmain64
    call kmain64

halt64:
    cli
    hlt
    jmp halt64

.section .bss
.align 16
kernel_stack_bottom:
    .skip 16384
kernel_stack_top:

/* Keep boot-time metadata in the low, identity-mapped boot image. */
.section .text.boot

/* Multiboot2 handoff values (written in 32-bit mode) */
.align 4
mb2_magic:
    .long 0
mb2_info:
    .long 0

/* 64-bit GDT: null, code, data */
.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF          /* 0x08: 64-bit code */
    .quad 0x00CF92000000FFFF          /* 0x10: data */
gdt64_end:

gdt64_ptr:
    .word (gdt64_end - gdt64 - 1)
    .long gdt64

.section .note.GNU-stack,"",@progbits
