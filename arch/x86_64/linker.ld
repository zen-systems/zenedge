/* arch/x86_64/linker.ld - Higher-half linker script (x86_64 bring-up) */

ENTRY(start)

KERN_BASE   = 0xFFFFFFFF80000000;
KERNEL_PBASE = 0x00100000;

SECTIONS {
    /* GRUB loads the ELF at low physical memory. */
    . = KERNEL_PBASE;

    /* Boot code runs identity-mapped (physical addresses). */
    .boot ALIGN(4K) : {
        _boot_start = .;
        *(.multiboot2)
        *(.text.boot)
        *(.text.boot.*)
        *(.rodata.boot)
        *(.rodata.boot.*)
        . = ALIGN(4K);
        _boot_end = .;
    }

    .boot_bss ALIGN(4K) (NOLOAD) : {
        _boot_bss_start = .;
        *(.bss.boot)
        *(.bss.boot.*)
        . = ALIGN(4K);
        _boot_bss_end = .;
    }

    _boot_phys_end = .;

    /* From here on, link at higher-half virtual addresses. */
    . += KERN_BASE;

    .text ALIGN(4K) : AT(ADDR(.text) - KERN_BASE) {
        _text_start = .;
        *(.text)
        *(.text.*)
        _text_end = .;
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERN_BASE) {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERN_BASE) {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4K) (NOLOAD) : AT(ADDR(.bss) - KERN_BASE) {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        . = ALIGN(4K);
        _bss_end = .;
    }

    _end = .;

    _kernel_phys_start = KERNEL_PBASE;
    _kernel_phys_end   = _end - KERN_BASE;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note.*)
    }
}

