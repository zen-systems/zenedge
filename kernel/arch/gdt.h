/* kernel/arch/gdt.h - Global Descriptor Table with TSS
 *
 * The GDT defines memory segments for protected mode operation.
 * For a flat memory model, we use overlapping segments covering all 4GB.
 *
 * Segment layout:
 *   0: Null descriptor (required by x86)
 *   1: Kernel code segment (ring 0) - 0x08
 *   2: Kernel data segment (ring 0) - 0x10
 *   3: User code segment (ring 3)   - 0x18
 *   4: User data segment (ring 3)   - 0x20
 *   5: TSS descriptor               - 0x28
 */

#ifndef _ARCH_GDT_H
#define _ARCH_GDT_H

#include <stdint.h>

/* Segment selectors (index << 3 | RPL) */
#define GDT_NULL_SEG        0x00
#define GDT_KERNEL_CODE_SEG 0x08
#define GDT_KERNEL_DATA_SEG 0x10
#define GDT_USER_CODE_SEG   0x1B  /* 0x18 | RPL 3 */
#define GDT_USER_DATA_SEG   0x23  /* 0x20 | RPL 3 */
#define GDT_TSS_SEG         0x28

/* GDT entry structure (8 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;      /* Limit bits 0-15 */
    uint16_t base_low;       /* Base bits 0-15 */
    uint8_t  base_mid;       /* Base bits 16-23 */
    uint8_t  access;         /* Access byte */
    uint8_t  granularity;    /* Limit bits 16-19 + flags */
    uint8_t  base_high;      /* Base bits 24-31 */
} gdt_entry_t;

/* GDT pointer structure for LGDT instruction */
typedef struct __attribute__((packed)) {
    uint16_t limit;          /* Size of GDT - 1 */
    uint32_t base;           /* Linear address of GDT */
} gdt_ptr_t;

/* Task State Segment (TSS) structure
 * Used for hardware task switching and ring transitions.
 * We mainly use it for the kernel stack pointer (esp0) when
 * transitioning from user mode (ring 3) to kernel mode (ring 0).
 */
typedef struct __attribute__((packed)) {
    uint32_t prev_tss;       /* Previous TSS (for hardware task switching) */
    uint32_t esp0;           /* Stack pointer for ring 0 */
    uint32_t ss0;            /* Stack segment for ring 0 */
    uint32_t esp1;           /* Stack pointer for ring 1 (unused) */
    uint32_t ss1;            /* Stack segment for ring 1 (unused) */
    uint32_t esp2;           /* Stack pointer for ring 2 (unused) */
    uint32_t ss2;            /* Stack segment for ring 2 (unused) */
    uint32_t cr3;            /* Page directory base */
    uint32_t eip;            /* Instruction pointer */
    uint32_t eflags;         /* Flags register */
    uint32_t eax;            /* General registers */
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;             /* Segment selectors */
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldtr;           /* LDT selector (unused) */
    uint16_t trap;           /* Trap on task switch flag */
    uint16_t iomap_base;     /* I/O map base address */
} tss_t;

/* Access byte flags */
#define GDT_ACCESS_PRESENT     (1 << 7)  /* Segment present */
#define GDT_ACCESS_RING0       (0 << 5)  /* Ring 0 (kernel) */
#define GDT_ACCESS_RING3       (3 << 5)  /* Ring 3 (user) */
#define GDT_ACCESS_SYSTEM      (0 << 4)  /* System segment (TSS, LDT) */
#define GDT_ACCESS_CODE_DATA   (1 << 4)  /* Code or data segment */
#define GDT_ACCESS_EXECUTABLE  (1 << 3)  /* Executable (code segment) */
#define GDT_ACCESS_DC          (1 << 2)  /* Direction/Conforming */
#define GDT_ACCESS_RW          (1 << 1)  /* Readable (code) / Writable (data) */
#define GDT_ACCESS_ACCESSED    (1 << 0)  /* Accessed flag */

/* Granularity byte flags */
#define GDT_GRAN_4K            (1 << 7)  /* 4KB granularity (vs 1B) */
#define GDT_GRAN_32BIT         (1 << 6)  /* 32-bit segment */
#define GDT_GRAN_LIMIT_HI(l)   ((l) & 0x0F)  /* High 4 bits of limit */

/* TSS type in access byte */
#define GDT_TSS_TYPE_32        0x09      /* 32-bit TSS (available) */
#define GDT_TSS_TYPE_32_BUSY   0x0B      /* 32-bit TSS (busy) */

/* Initialize GDT and TSS */
void gdt_init(void);

/* Set kernel stack in TSS (called during context switch) */
void gdt_set_kernel_stack(uint32_t stack);

#endif /* _ARCH_GDT_H */
