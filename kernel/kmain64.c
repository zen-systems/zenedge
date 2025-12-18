/* kernel/kmain64.c - Milestone M0: long-mode banner */
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void serial_init(void) {
  /* 115200 8N1, FIFO enabled; safe defaults for QEMU. */
  outb(0x3F8 + 1, 0x00); /* Disable interrupts */
  outb(0x3F8 + 3, 0x80); /* Enable DLAB */
  outb(0x3F8 + 0, 0x01); /* Divisor low  (115200) */
  outb(0x3F8 + 1, 0x00); /* Divisor high */
  outb(0x3F8 + 3, 0x03); /* 8 bits, no parity, one stop bit */
  outb(0x3F8 + 2, 0xC7); /* Enable FIFO, clear, 14-byte threshold */
  outb(0x3F8 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

static void serial_write(const char *s) {
  while (*s) outb(0x3F8, (uint8_t)*s++);
}

static void serial_write_hex64(uint64_t v) {
  static const char *hex = "0123456789abcdef";
  char buf[2 + 16 + 2 + 1];
  buf[0] = '0';
  buf[1] = 'x';
  for (int i = 0; i < 16; i++) {
    buf[2 + i] = hex[(v >> (60 - (i * 4))) & 0xF];
  }
  buf[18] = '\r';
  buf[19] = '\n';
  buf[20] = '\0';
  serial_write(buf);
}

void kmain64(uint32_t mb2_magic, uint32_t mb2_info_ptr) {
  (void)mb2_magic;
  (void)mb2_info_ptr;

  serial_init();
  serial_write("[boot] long mode ok\n");
  serial_write("[boot] kmain64 @ ");
  serial_write_hex64((uint64_t)(uintptr_t)&kmain64);

  for (;;) __asm__ __volatile__("hlt");
}
