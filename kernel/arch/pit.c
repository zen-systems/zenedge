/* kernel/arch/pit.c - PIT driver implementation
 *
 * Configures the PIT channel 0 as a rate generator for system tick.
 */

#include "pit.h"
#include "pic.h"
#include "../console.h"

/* I/O port helpers */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Current tick rate */
static uint32_t tick_rate_hz = 0;

/* External timer tick counter (in pic.c) */
extern uint32_t timer_get_ticks(void);

/* Helper: print a number */
static void print_num(uint32_t val) {
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

void pit_init(uint32_t frequency) {
    console_write("[pit] configuring timer at ");
    print_num(frequency);
    console_write(" Hz\n");

    /* Calculate divisor */
    uint32_t divisor = PIT_BASE_FREQ / frequency;

    /* Clamp divisor to 16 bits */
    if (divisor > 65535) {
        divisor = 65535;
    }
    if (divisor < 1) {
        divisor = 1;
    }

    tick_rate_hz = PIT_BASE_FREQ / divisor;

    /* Configure channel 0: rate generator mode, access low/high byte */
    outb(PIT_CMD, PIT_SEL_CHAN0 | PIT_ACCESS_LOHI | PIT_MODE_2);

    /* Set divisor (low byte first, then high byte) */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    /* Unmask IRQ0 (timer) */
    pic_unmask_irq(0);

    console_write("[pit] timer configured, IRQ0 unmasked\n");
}

uint32_t pit_get_ticks(void) {
    return timer_get_ticks();
}

void pit_sleep_ms(uint32_t ms) {
    if (tick_rate_hz == 0) {
        return;  /* Timer not initialized */
    }

    /* Calculate number of ticks to wait */
    uint32_t ticks = (ms * tick_rate_hz) / 1000;
    if (ticks == 0) {
        ticks = 1;
    }

    uint32_t start = timer_get_ticks();
    while ((timer_get_ticks() - start) < ticks) {
        /* Halt until next interrupt to save power */
        __asm__ __volatile__("hlt");
    }
}
