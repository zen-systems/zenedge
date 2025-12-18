/* kernel/time/time.h
 *
 * Real-time measurement for AI/ML workload telemetry.
 * Uses rdtsc (cycle counter) calibrated against PIT for wall-clock conversion.
 */
#ifndef TIME_H
#define TIME_H

#include <stdint.h>

/*
 * Time representation: cycles and microseconds
 * Cycles are raw rdtsc values - monotonic, high resolution
 * Microseconds are derived via calibration
 */
typedef uint64_t cycles_t;
typedef uint64_t usec_t;

/* Initialize time subsystem - calibrates rdtsc against PIT */
void time_init(void);

/* Read current cycle counter (rdtsc) - very fast, no syscall */
static inline cycles_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Get current time in cycles */
cycles_t time_cycles(void);

/* Get current time in microseconds (since boot) */
usec_t time_usec(void);

/* Convert cycles to microseconds */
usec_t cycles_to_usec(cycles_t cycles);

/* Convert microseconds to cycles */
cycles_t usec_to_cycles(usec_t usec);

/* Get CPU frequency in MHz (after calibration) */
uint32_t time_get_cpu_mhz(void);

/*
 * Duration measurement helpers
 * Usage:
 *   cycles_t start = time_cycles();
 *   // ... work ...
 *   usec_t elapsed = time_elapsed_usec(start);
 */
static inline usec_t time_elapsed_usec(cycles_t start) {
    return cycles_to_usec(time_cycles() - start);
}

static inline cycles_t time_elapsed_cycles(cycles_t start) {
    return time_cycles() - start;
}

#endif /* TIME_H */
