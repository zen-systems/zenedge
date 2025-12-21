/* kernel/arch/pic.c - 8259 PIC driver implementation
 *
 * Initializes the cascaded 8259 PICs and remaps IRQs to vectors 32-47.
 */

#include "pic.h"
#include "../console.h"
#include "idt.h"

/* I/O port helpers */
static inline void outb(uint16_t port, uint8_t val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

/* Small delay for PIC operations */
static inline void io_wait(void) {
  /* Port 0x80 is used for POST codes, writing to it causes a tiny delay */
  outb(0x80, 0);
}

/* IRQ handlers array */
static interrupt_handler_t irq_handlers[16];

/* Timer tick counter */
static volatile uint32_t timer_ticks = 0;

#include "../sched/sched_core.h"

/* Default timer handler - just count ticks */
static void timer_handler(interrupt_frame_t *frame) {
  (void)frame;
  timer_ticks++;
#ifndef __x86_64__
  schedule();
#endif
}

/* Register an IRQ handler */
void irq_register_handler(uint8_t irq, interrupt_handler_t handler) {
  if (irq < 16) {
    irq_handlers[irq] = handler;
  }
}

/* IRQ dispatcher - called from assembly */
void irq_handler(interrupt_frame_t *frame) {
  /* Get IRQ number from interrupt vector */
  uint8_t irq = frame->int_no - PIC1_OFFSET;

  /* Handle spurious IRQ7 and IRQ15 */
  if (irq == 7) {
    uint16_t isr = pic_get_isr();
    if (!(isr & (1 << 7))) {
      /* Spurious - don't send EOI */
      return;
    }
  } else if (irq == 15) {
    uint16_t isr = pic_get_isr();
    if (!(isr & (1 << 15))) {
      /* Spurious - send EOI to master only */
      outb(PIC1_CMD, PIC_EOI);
      return;
    }
  }

  /* Send EOI to PIC(s) BEFORE handler
   * This is crucial for the scheduler: if the handler switches tasks,
   * we must have already ACKed the interrupt, otherwise the PIC remains
   * blocked until the task resumes (which might be never if it's a new task).
   *
   * Since interrupts are disabled here (from ISR stub), it's safe to ACK.
   */
  pic_send_eoi(irq);

  /* Call registered handler */
  if (irq_handlers[irq]) {
    irq_handlers[irq](frame);
  }
}

void pic_init(void) {
  console_write("[pic] remapping IRQs to vectors 32-47...\n");

  /* Save current masks */
  uint8_t mask1 = inb(PIC1_DATA);
  uint8_t mask2 = inb(PIC2_DATA);

  /* Start initialization sequence (ICW1) */
  outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
  io_wait();
  outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
  io_wait();

  /* Set vector offsets (ICW2) */
  outb(PIC1_DATA, PIC1_OFFSET); /* Master: IRQ 0-7  -> vectors 32-39 */
  io_wait();
  outb(PIC2_DATA, PIC2_OFFSET); /* Slave:  IRQ 8-15 -> vectors 40-47 */
  io_wait();

  /* Set cascade identity (ICW3) */
  outb(PIC1_DATA, 0x04); /* Master: slave on IRQ2 (bit 2) */
  io_wait();
  outb(PIC2_DATA, 0x02); /* Slave: cascade identity 2 */
  io_wait();

  /* Set 8086 mode (ICW4) */
  outb(PIC1_DATA, ICW4_8086);
  io_wait();
  outb(PIC2_DATA, ICW4_8086);
  io_wait();

  /* Restore masks (mask all for now) */
  outb(PIC1_DATA, 0xFF); /* Mask all IRQs on master */
  outb(PIC2_DATA, 0xFF); /* Mask all IRQs on slave */

  /* Clear IRQ handlers */
  for (int i = 0; i < 16; i++) {
    irq_handlers[i] = 0;
  }

  /* Register default timer handler */
  irq_handlers[0] = timer_handler;

  console_write("[pic] PIC initialized, all IRQs masked\n");
}

void pic_send_eoi(uint8_t irq) {
  /* If IRQ came from slave PIC, send EOI to both */
  if (irq >= 8) {
    outb(PIC2_CMD, PIC_EOI);
  }
  outb(PIC1_CMD, PIC_EOI);
}

void pic_mask_irq(uint8_t irq) {
  uint16_t port;
  uint8_t mask;

  if (irq < 8) {
    port = PIC1_DATA;
  } else {
    port = PIC2_DATA;
    irq -= 8;
  }

  mask = inb(port);
  mask |= (1 << irq);
  outb(port, mask);
}

void pic_unmask_irq(uint8_t irq) {
  uint16_t port;
  uint8_t mask;

  if (irq < 8) {
    port = PIC1_DATA;
  } else {
    port = PIC2_DATA;
    irq -= 8;
    /* Also unmask cascade IRQ on master */
    mask = inb(PIC1_DATA);
    mask &= ~(1 << 2);
    outb(PIC1_DATA, mask);
  }

  mask = inb(port);
  mask &= ~(1 << irq);
  outb(port, mask);
}

void pic_disable(void) {
  outb(PIC1_DATA, 0xFF);
  outb(PIC2_DATA, 0xFF);
}

uint16_t pic_get_isr(void) {
  /* Read In-Service Register */
  outb(PIC1_CMD, 0x0B);
  outb(PIC2_CMD, 0x0B);
  return (inb(PIC2_CMD) << 8) | inb(PIC1_CMD);
}

uint16_t pic_get_irr(void) {
  /* Read Interrupt Request Register */
  outb(PIC1_CMD, 0x0A);
  outb(PIC2_CMD, 0x0A);
  return (inb(PIC2_CMD) << 8) | inb(PIC1_CMD);
}

/* Get current timer tick count */
uint32_t timer_get_ticks(void) { return timer_ticks; }
