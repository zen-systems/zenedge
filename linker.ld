/* linker.ld - Higher-half kernel linker script for ZENEDGE
 *
 * The kernel is loaded at physical address 1MB (0x100000) but
 * linked to run at virtual address 0xC0000000 + 1MB = 0xC0100000
 *
 * Layout:
 *   Physical: 0x00100000 (1MB)
 *   Virtual:  0xC0100000 (KERNEL_VBASE + 1MB)
 *
 * Boot code (.boot section) runs with identity mapping at physical addresses.
 * Main kernel runs at virtual addresses after paging is enabled.
 */

ENTRY(start)

/* Virtual address where kernel is mapped */
KERNEL_VBASE = 0xC0000000;

/* Physical load address (1MB, required by multiboot) */
KERNEL_PBASE = 0x00100000;

SECTIONS {
    /* Physical load address for multiboot */
    . = KERNEL_PBASE;

    /* Boot section - runs at physical addresses before paging
     * This includes the multiboot header and boot code
     */
    .boot ALIGN(4K) : {
        _boot_start = .;
        *(.multiboot)           /* Multiboot header must be first */
        *boot/start.o(.text)    /* Boot code (allow for out-of-tree OBJDIR) */
        . = ALIGN(4K);
        _boot_end = .;
    }

    /* Boot data - page tables and stack at physical addresses
     * These need to be at known physical addresses for paging setup
     */
    .boot_bss ALIGN(4K) (NOLOAD) : {
        _boot_bss_start = .;
        *boot/start.o(.bss)     /* Boot BSS (page tables, stack) */
        . = ALIGN(4K);
        _boot_bss_end = .;
    }

    /* Record where boot ends physically */
    _boot_phys_end = .;

    /* From here on, use virtual addresses
     * We add KERNEL_VBASE to get virtual addresses
     * AT() specifies the physical load address
     */
    . += KERNEL_VBASE;

    /* Text section at virtual address */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VBASE) {
        _text_start = .;
        *(.text)
        *(.text.*)
        _text_end = .;
    }

    /* Read-only data */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VBASE) {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        _rodata_end = .;
    }

    /* Initialized data */
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VBASE) {
        _data_start = .;
        *(.data)
        *(.data.*)
        _data_end = .;
    }

    /* Uninitialized data (BSS) */
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VBASE) {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        . = ALIGN(4K);
        _bss_end = .;
    }

    /* End of kernel (virtual address) */
    _end = .;

    /* Physical addresses for PMM */
    _kernel_phys_start = KERNEL_PBASE;
    _kernel_phys_end = _end - KERNEL_VBASE;

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note.*)
    }
}
