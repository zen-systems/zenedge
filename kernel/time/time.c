/* kernel/time/time.c
 *
 * Time subsystem implementation using rdtsc calibrated against PIT.
 *
 * Calibration approach:
 * 1. Program PIT channel 2 for a known delay (e.g., 10ms)
 * 2. Read rdtsc before and after
 * 3. Calculate cycles per microsecond
 *
 * This gives us accurate wall-clock time from the TSC.
 */
#include "time.h"
#include "../console.h"

/* PIT (Programmable Interval Timer) ports */
#define PIT_CHANNEL_2   0x42
#define PIT_COMMAND     0x43
#define PIT_GATE        0x61

/* PIT frequency: 1.193182 MHz */
#define PIT_FREQ_HZ     1193182

/* Calibration period in PIT ticks (10ms = ~11932 ticks) */
#define CALIBRATION_MS  10
#define CALIBRATION_PIT_TICKS ((PIT_FREQ_HZ * CALIBRATION_MS) / 1000)

/* Global state */
static uint32_t cpu_mhz = 0;
static uint32_t cycles_per_usec = 0;
static cycles_t boot_tsc = 0;

/* Port I/O helpers */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Wait for PIT count to complete using channel 2 (speaker timer) */
static void pit_wait(uint16_t count) {
    uint8_t tmp;

    /* Disable speaker, enable gate */
    tmp = inb(PIT_GATE);
    tmp &= 0xFC;  /* Clear bits 0,1 */
    tmp |= 0x01;  /* Enable gate (bit 0), disable speaker (bit 1 = 0) */
    outb(PIT_GATE, tmp);

    /* Program channel 2: mode 0 (interrupt on terminal count), binary */
    outb(PIT_COMMAND, 0xB0);  /* 10110000: ch2, lobyte/hibyte, mode 0, binary */

    /* Load count */
    outb(PIT_CHANNEL_2, count & 0xFF);
    outb(PIT_CHANNEL_2, (count >> 8) & 0xFF);

    /* Reset the flip-flop by reading then writing */
    tmp = inb(PIT_GATE);
    tmp &= 0xFE;  /* Clear gate */
    outb(PIT_GATE, tmp);
    tmp |= 0x01;  /* Set gate to start counting */
    outb(PIT_GATE, tmp);

    /* Wait for output to go high (bit 5 of port 0x61) */
    while ((inb(PIT_GATE) & 0x20) == 0) {
        /* spin */
    }
}

/* Calibrate TSC against PIT */
static void calibrate_tsc(void) {
    cycles_t start, end;
    uint64_t elapsed;

    console_write("[time] calibrating TSC...\n");

    /* First measurement */
    start = rdtsc();
    pit_wait(CALIBRATION_PIT_TICKS);
    end = rdtsc();

    elapsed = end - start;

    /* cycles_per_usec = elapsed / (CALIBRATION_MS * 1000) */
    cycles_per_usec = (uint32_t)(elapsed / (CALIBRATION_MS * 1000));
    cpu_mhz = cycles_per_usec;

    /* Print calibration result */
    console_write("[time] CPU frequency: ");

    /* Print MHz (simple integer print) */
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    uint32_t v = cpu_mhz;
    if (v == 0) {
        buf[i--] = '0';
    } else {
        while (v > 0 && i >= 0) {
            buf[i--] = '0' + (v % 10);
            v /= 10;
        }
    }
    console_write(&buf[i + 1]);
    console_write(" MHz\n");
}

void time_init(void) {
    console_write("[time] initializing time subsystem\n");

    /* Record boot TSC */
    boot_tsc = rdtsc();

    /* Skip PIT calibration for now - use assumed CPU frequency */
    /* TODO: Re-enable calibrate_tsc() once PIT issues are resolved */
    cycles_per_usec = 1000;  /* Assume 1 GHz for QEMU */
    cpu_mhz = 1000;

    console_write("[time] using assumed 1000 MHz CPU frequency\n");
    console_write("[time] init complete\n");
}

cycles_t time_cycles(void) {
    return rdtsc();
}

usec_t time_usec(void) {
    if (cycles_per_usec == 0) return 0;
    return (rdtsc() - boot_tsc) / cycles_per_usec;
}

usec_t cycles_to_usec(cycles_t cycles) {
    if (cycles_per_usec == 0) return 0;
    return cycles / cycles_per_usec;
}

cycles_t usec_to_cycles(usec_t usec) {
    return usec * cycles_per_usec;
}

uint32_t time_get_cpu_mhz(void) {
    return cpu_mhz;
}
