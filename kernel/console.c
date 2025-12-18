/* kernel/console.c */
#include "console.h"

/* Serial port (COM1) */
#define SERIAL_PORT 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static int serial_enabled = 0;

static void serial_init(void) {
  outb(SERIAL_PORT + 1, 0x00); /* Disable all interrupts */
  outb(SERIAL_PORT + 3, 0x80); /* Enable DLAB (set baud rate divisor) */
  outb(SERIAL_PORT + 0, 0x03); /* Set divisor to 3 (lo byte) 38400 baud */
  outb(SERIAL_PORT + 1, 0x00); /*                  (hi byte) */
  outb(SERIAL_PORT + 3, 0x03); /* 8 bits, no parity, one stop bit */
  outb(SERIAL_PORT + 2, 0xC7); /* Enable FIFO, clear them, 14-byte threshold */
  outb(SERIAL_PORT + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
  serial_enabled = 1;
}

static void serial_putc(char c) {
  if (!serial_enabled)
    return;
  /* Wait for transmit empty */
  while ((inb(SERIAL_PORT + 5) & 0x20) == 0)
    ;
  outb(SERIAL_PORT, c);
}

/* VGA buffer */
static uint16_t *const VGA_BUFFER = (uint16_t *)0xB8000;
static const int VGA_WIDTH = 80;
static const int VGA_HEIGHT = 25;

static int cursor_row = 0;
static int cursor_col = 0;
static uint8_t default_color = 0x0F; /* white on black */

static uint16_t vga_entry(char c, uint8_t color) {
  return (uint16_t)c | ((uint16_t)color << 8);
}

void console_cls(void) {
  /* Initialize serial port on first call */
  if (!serial_enabled) {
    serial_init();
  }

  for (int y = 0; y < VGA_HEIGHT; y++) {
    for (int x = 0; x < VGA_WIDTH; x++) {
      VGA_BUFFER[y * VGA_WIDTH + x] = vga_entry(' ', default_color);
    }
  }
  cursor_row = 0;
  cursor_col = 0;
}

void print_uint(uint32_t val) {
  char buf[12];
  int i = 10;
  buf[11] = '\0';
  if (val == 0) {
    console_write("0");
    return;
  }
  while (val > 0 && i >= 0) {
    buf[i--] = '0' + (val % 10);
    val /= 10;
  }
  console_write(&buf[i + 1]);
}

void print_hex32(uint32_t val) {
  static const char hex[] = "0123456789ABCDEF";
  char buf[11]; // "0x" + 8 chars + null
  buf[0] = '0';
  buf[1] = 'x';
  for (int i = 0; i < 8; i++) {
    buf[2 + i] = hex[(val >> (28 - i * 4)) & 0xF];
  }
  buf[10] = '\0';
  console_write(buf);
}

static void console_scroll(void) {
  /* Move lines 1-24 up to 0-23 */
  for (int y = 0; y < VGA_HEIGHT - 1; y++) {
    for (int x = 0; x < VGA_WIDTH; x++) {
      VGA_BUFFER[y * VGA_WIDTH + x] = VGA_BUFFER[(y + 1) * VGA_WIDTH + x];
    }
  }

  /* Clear last line */
  for (int x = 0; x < VGA_WIDTH; x++) {
    VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
        vga_entry(' ', default_color);
  }
}

void console_putc(char c) {
  /* Output to serial port */
  if (c == '\n') {
    serial_putc('\r'); /* Serial needs CR+LF */
  }
  serial_putc(c);

  /* Backspace handling */
  if (c == '\b') {
    if (cursor_col > 0) {
      cursor_col--;
    } else if (cursor_row > 0) {
      cursor_row--;
      cursor_col = VGA_WIDTH - 1;
    }
    VGA_BUFFER[cursor_row * VGA_WIDTH + cursor_col] =
        vga_entry(' ', default_color);
    return;
  }

  /* Output to VGA */
  if (c == '\n') {
    cursor_col = 0;
    cursor_row++;
  } else if (c >= ' ') { /* Printable character */
    VGA_BUFFER[cursor_row * VGA_WIDTH + cursor_col] =
        vga_entry(c, default_color);
    cursor_col++;
    if (cursor_col >= VGA_WIDTH) {
      cursor_col = 0;
      cursor_row++;
    }
  }

  /* Scroll if needed */
  if (cursor_row >= VGA_HEIGHT) {
    console_scroll();
    cursor_row = VGA_HEIGHT - 1;
  }
}

void console_write(const char *str) {
  while (*str) {
    console_putc(*str++);
  }
}
