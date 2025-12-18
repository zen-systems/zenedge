#include "keyboard.h"
#include "../console.h"
#include "idt.h"
#include "pic.h"
#include "scancodes.h"

/* Ring buffer for keyboard input */
#define KB_BUF_SIZE 128
static char kb_buffer[KB_BUF_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;

/* Internal state */
static int shift_pressed = 0;

static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

/* IRQ 1 is vector 33 (32 + 1) */
#define IRQ1 33

void keyboard_handler(interrupt_frame_t *regs) {
  (void)regs;

  /* Read scancode from keyboard controller */
  uint8_t scancode = inb(0x60);

  /* Check for key release (high bit set) */
  if (scancode & 0x80) {
    /* Key release */
    scancode &= 0x7F;
    if (scancode == 0x2A || scancode == 0x36) { /* Left/Right Shift */
      shift_pressed = 0;
    }
  } else {
    /* Key press */
    if (scancode == 0x2A || scancode == 0x36) {
      shift_pressed = 1;
    } else {
      /* Convert to ASCII */
      char c = 0;
      if (scancode < sizeof(scancode_map)) {
        if (shift_pressed) {
          c = scancode_map_shift[scancode];
        } else {
          c = scancode_map[scancode];
        }
      }

      /* Add to buffer if valid char and space available */
      if (c) {
        int next = (kb_head + 1) % KB_BUF_SIZE;
        if (next != kb_tail) {
          kb_buffer[kb_head] = c;
          kb_head = next;

          /* Echo to console immediately for feedback */
          /* Note: real shell might handle echo, but this is fine for now */
          // console_putc(c);
        }
      }
    }
  }
}

void keyboard_init(void) {
  /* Register IRQ 1 handler */
  idt_register_handler(IRQ1, keyboard_handler);

  /* Unmask IRQ 1 (Keyboard) */
  pic_unmask_irq(1);

  console_write("[keyboard] initialized\n");
}

char tc_getchar(void) {
  /* Wait for input */
  while (kb_head == kb_tail) {
    __asm__ __volatile__("hlt");
  }

  char c = kb_buffer[kb_tail];
  kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
  return c;
}

int tc_has_input(void) { return kb_head != kb_tail; }
