/* boot/start.s - ZENEDGE Higher-Half Kernel Boot Code
 *
 * This code runs at physical addresses before paging is enabled.
 * It sets up page tables, enables paging, and jumps to the
 * kernel at its higher-half virtual address (0xC0100000).
 *
 * Memory layout after paging:
 *   0x00000000 - 0x003FFFFF: Identity mapped (first 4MB, for boot transition)
 *   0xC0000000 - 0xC7FFFFFF: Kernel (maps physical 0x00000000 - 0x07FFFFFF = 128MB)
 *
 * We use 4MB PSE (Page Size Extension) pages for simplicity and to map
 * all of physical RAM so that phys_to_virt() works for any address.
 */

.set KERNEL_VBASE,    0xC0000000
.set KERNEL_PDE_IDX,  (KERNEL_VBASE >> 22)  /* = 768 */

/* Page table flags */
.set PTE_PRESENT,     0x001
.set PTE_WRITABLE,    0x002
.set PTE_PSE,         0x080    /* 4MB page (Page Size Extension) */

/* CR0 bits */
.set CR0_PG,          0x80000000  /* Paging enable */
.set CR0_WP,          0x00010000  /* Write protect */

/* CR4 bits */
.set CR4_PSE,         0x00000010  /* Page Size Extension enable */

/* Multiboot header is in multiboot_header.s */

.section .bss
.align 4096
/* Reserve space for initial page directory */
boot_page_directory:
    .skip 4096

/* Note: With PSE, we don't need page tables - PDEs point directly to 4MB pages */

/* Boot stack */
.align 16
boot_stack_bottom:
    .skip 16384         /* 16KB boot stack */
boot_stack_top:

.section .text
.global start
.global boot_page_directory

start:
    /* Disable interrupts */
    cli

    /* Save multiboot info pointer (physical address)
     * EBX contains the multiboot info pointer from GRUB
     * Save it to ESI which we won't touch until kmain
     */
    mov %ebx, %esi

    /* Output boot marker to serial */
    mov $0x3F8, %dx
    mov $'Z', %al
    out %al, %dx
    mov $'E', %al
    out %al, %dx
    mov $'>', %al
    out %al, %dx

    /* Set up boot stack (physical address for now) */
    mov $boot_stack_top, %esp

    /*
     * Enable PSE (Page Size Extension) in CR4 BEFORE setting up page tables.
     * This allows us to use 4MB pages.
     */
    mov %cr4, %eax
    or $CR4_PSE, %eax
    mov %eax, %cr4

    /*
     * Set up page directory for higher-half kernel using 4MB PSE pages.
     *
     * We need:
     * 1. Identity map first 4MB (PDE[0] -> physical 0x00000000)
     *    So boot code continues to work after paging is enabled
     * 2. Map 128MB at 0xC0000000 (PDE[768-799] -> physical 0x00000000-0x07FFFFFF)
     *    This is where the kernel will run AND where phys_to_virt() expects
     *    all physical RAM to be accessible.
     *
     * With PSE, each PDE directly maps 4MB (no page tables needed).
     * ESI holds multiboot pointer - DON'T TOUCH IT!
     */

    /* First, zero out the page directory */
    mov $boot_page_directory, %edi
    mov $1024, %ecx
    xor %eax, %eax
    rep stosl

    /* Set up PDE[0] - identity map first 4MB using PSE */
    mov $boot_page_directory, %edi
    mov $(0x00000000 | PTE_PRESENT | PTE_WRITABLE | PTE_PSE), %eax
    mov %eax, (%edi)                /* PDE[0] -> phys 0x00000000 (4MB) */

    /* Set up PDE[768-799] - map 128MB at 0xC0000000 using PSE
     * 128MB / 4MB = 32 entries
     */
    mov $boot_page_directory, %edi
    add $(KERNEL_PDE_IDX * 4), %edi /* Point to PDE[768] */
    mov $0x00000000, %eax           /* Start at physical address 0 */
    mov $32, %ecx                   /* 32 entries = 128MB */

1:  mov %eax, %edx
    or $(PTE_PRESENT | PTE_WRITABLE | PTE_PSE), %edx
    mov %edx, (%edi)                /* Store PDE */
    add $0x400000, %eax             /* Next 4MB physical region */
    add $4, %edi                    /* Next PDE */
    dec %ecx
    jnz 1b

    /* Output page table setup marker */
    mov $0x3F8, %dx
    mov $'P', %al
    out %al, %dx
    mov $'S', %al                   /* 'S' for PSE */
    out %al, %dx

    /* Load page directory into CR3 */
    mov $boot_page_directory, %eax
    mov %eax, %cr3

    /* Enable paging (CR0.PG = 1)
     * Also enable write protection (CR0.WP = 1) so kernel can't
     * accidentally write to read-only pages
     */
    mov %cr0, %eax
    or $(CR0_PG | CR0_WP), %eax
    mov %eax, %cr0

    /* Output paging enabled marker */
    mov $0x3F8, %dx
    mov $'V', %al
    out %al, %dx
    mov $'M', %al
    out %al, %dx

    /*
     * Paging is now enabled!
     * We're still executing at low addresses due to identity map.
     * Now jump to the higher-half address.
     */

    /* Load the virtual address of higher_half and jump */
    lea higher_half, %eax
    jmp *%eax

higher_half:
    /*
     * We're now running at virtual addresses (0xC0xxxxxx)
     *
     * We can now:
     * 1. Remove the identity mapping (optional, but cleaner)
     * 2. Set up the real stack at a higher-half address
     * 3. Call kmain
     */

    /* Output higher-half marker */
    mov $0x3F8, %dx
    mov $'H', %al
    out %al, %dx
    mov $'H', %al
    out %al, %dx
    mov $'\r', %al
    out %al, %dx
    mov $'\n', %al
    out %al, %dx

    /* Set up stack at higher-half address
     * boot_stack_top is now a virtual address thanks to linking
     */
    mov $boot_stack_top, %esp
    and $0xFFFFFFF0, %esp           /* 16-byte alignment */
    sub $8, %esp                    /* Adjust for ABI */
    mov %esp, %ebp

    /* Multiboot info pointer is in ESI (physical address)
     * The identity map (PDE[0]) is still in place, so we can
     * access low physical addresses directly.
     * Pass the physical address - it works because of identity map.
     */
    push %esi                       /* Pass multiboot info pointer */

    /* Call the kernel entry point (kmain)
     * kmain is linked at virtual address
     */
    .extern kmain
    call kmain

    /* If kmain returns, output marker and halt */
    mov $0x3F8, %dx
    mov $'!', %al
    out %al, %dx

halt:
    cli
    hlt
    jmp halt
