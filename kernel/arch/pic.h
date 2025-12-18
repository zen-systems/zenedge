/* kernel/arch/pic.h - 8259 Programmable Interrupt Controller
 *
 * The 8259 PIC is the legacy interrupt controller for x86.
 * There are two PICs: master (IRQ 0-7) and slave (IRQ 8-15).
 *
 * By default, the PIC maps IRQs to vectors 0-15, which conflicts
 * with CPU exceptions. We remap them to vectors 32-47.
 */

#ifndef _ARCH_PIC_H
#define _ARCH_PIC_H

#include <stdint.h>

/* PIC I/O ports */
#define PIC1_CMD        0x20    /* Master PIC command port */
#define PIC1_DATA       0x21    /* Master PIC data port */
#define PIC2_CMD        0xA0    /* Slave PIC command port */
#define PIC2_DATA       0xA1    /* Slave PIC data port */

/* PIC commands */
#define PIC_EOI         0x20    /* End of Interrupt */

/* ICW1 (Initialization Command Word 1) */
#define ICW1_ICW4       0x01    /* ICW4 will be present */
#define ICW1_SINGLE     0x02    /* Single (vs cascade) mode */
#define ICW1_INTERVAL4  0x04    /* Call address interval 4 (vs 8) */
#define ICW1_LEVEL      0x08    /* Level triggered (vs edge) mode */
#define ICW1_INIT       0x10    /* Initialization flag (required) */

/* ICW4 (Initialization Command Word 4) */
#define ICW4_8086       0x01    /* 8086/88 mode (vs MCS-80/85) */
#define ICW4_AUTO       0x02    /* Auto EOI */
#define ICW4_BUF_SLAVE  0x08    /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C    /* Buffered mode/master */
#define ICW4_SFNM       0x10    /* Special fully nested mode */

/* IRQ base vectors (after remapping) */
#define PIC1_OFFSET     32      /* IRQ 0-7  -> vectors 32-39 */
#define PIC2_OFFSET     40      /* IRQ 8-15 -> vectors 40-47 */

/* Initialize the PICs and remap IRQs */
void pic_init(void);

/* Send End of Interrupt signal */
void pic_send_eoi(uint8_t irq);

/* Mask (disable) an IRQ */
void pic_mask_irq(uint8_t irq);

/* Unmask (enable) an IRQ */
void pic_unmask_irq(uint8_t irq);

/* Disable all IRQs (mask all) */
void pic_disable(void);

/* Get combined ISR (In-Service Register) */
uint16_t pic_get_isr(void);

/* Get combined IRR (Interrupt Request Register) */
uint16_t pic_get_irr(void);

#endif /* _ARCH_PIC_H */
