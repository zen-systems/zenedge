/* arch/x86_64/boot/multiboot2_header.s */
.section .multiboot2
.align 8

/* Multiboot2 header (GRUB uses `multiboot2 /boot/zenedge.bin`) */
.set MB2_MAGIC, 0xE85250D6
.set MB2_ARCH,  0
.set MB2_LEN,   (header_end - header_start)
.set MB2_CSUM,  -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_LEN
    .long MB2_CSUM

    /* End tag */
    .word 0
    .word 0
    .long 8

header_end:

.section .note.GNU-stack,"",@progbits
