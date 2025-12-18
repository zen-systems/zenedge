/* boot/multiboot_header.s */
.section .multiboot
    .align 4

    /* Magic number for Multiboot */
    .long 0x1BADB002
    /* Flags: align modules on page boundaries, provide memory info */
    .long 0x00000003
    /* Checksum: magic + flags + checksum = 0 */
    .long -(0x1BADB002 + 0x00000003)

