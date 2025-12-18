/* kernel/arch/pit.h - 8253/8254 Programmable Interval Timer
 *
 * The PIT generates periodic interrupts for the system timer.
 * Channel 0 is connected to IRQ0 and used for the system tick.
 */

#ifndef _ARCH_PIT_H
#define _ARCH_PIT_H

#include <stdint.h>

/* PIT I/O ports */
#define PIT_CHANNEL0    0x40    /* Channel 0 data port */
#define PIT_CHANNEL1    0x41    /* Channel 1 data port */
#define PIT_CHANNEL2    0x42    /* Channel 2 data port */
#define PIT_CMD         0x43    /* Mode/Command register */

/* PIT command byte bits */
#define PIT_SEL_CHAN0   (0 << 6)    /* Select channel 0 */
#define PIT_SEL_CHAN1   (1 << 6)    /* Select channel 1 */
#define PIT_SEL_CHAN2   (2 << 6)    /* Select channel 2 */
#define PIT_SEL_READBACK (3 << 6)   /* Read-back command */

#define PIT_ACCESS_LATCH (0 << 4)   /* Latch count value */
#define PIT_ACCESS_LO    (1 << 4)   /* Access low byte only */
#define PIT_ACCESS_HI    (2 << 4)   /* Access high byte only */
#define PIT_ACCESS_LOHI  (3 << 4)   /* Access low then high byte */

#define PIT_MODE_0       (0 << 1)   /* Interrupt on terminal count */
#define PIT_MODE_1       (1 << 1)   /* Hardware re-triggerable one-shot */
#define PIT_MODE_2       (2 << 1)   /* Rate generator */
#define PIT_MODE_3       (3 << 1)   /* Square wave generator */
#define PIT_MODE_4       (4 << 1)   /* Software triggered strobe */
#define PIT_MODE_5       (5 << 1)   /* Hardware triggered strobe */

#define PIT_BCD          (1 << 0)   /* BCD mode (vs binary) */

/* PIT base frequency (Hz) */
#define PIT_BASE_FREQ   1193182

/* Default tick rate (Hz) - ~100 Hz gives ~10ms resolution */
#define PIT_DEFAULT_HZ  100

/* Initialize PIT with given frequency (Hz) */
void pit_init(uint32_t frequency);

/* Get current tick count */
uint32_t pit_get_ticks(void);

/* Sleep for approximately the given number of milliseconds */
void pit_sleep_ms(uint32_t ms);

#endif /* _ARCH_PIT_H */
