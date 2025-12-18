/* kernel/lib/divdi3.c
 *
 * Minimal 64-bit division support for i386 bare-metal.
 * These functions are called by the compiler for 64-bit arithmetic on 32-bit.
 */

#include <stdint.h>

/* 64-bit unsigned division */
uint64_t __udivdi3(uint64_t n, uint64_t d) {
    if (d == 0) return 0;  /* Avoid divide by zero */

    /* Fast path: if divisor fits in 32 bits and is power of 2 */
    if (d <= 0xFFFFFFFF) {
        uint32_t d32 = (uint32_t)d;

        /* Check if power of 2 */
        if ((d32 & (d32 - 1)) == 0) {
            int shift = 0;
            while ((d32 >> shift) != 1) shift++;
            return n >> shift;
        }
    }

    /* General case: binary long division */
    uint64_t q = 0;
    uint64_t r = 0;

    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (1ULL << i);
        }
    }

    return q;
}

/* 64-bit unsigned modulo */
uint64_t __umoddi3(uint64_t n, uint64_t d) {
    if (d == 0) return 0;

    /* Fast path: power of 2 */
    if (d <= 0xFFFFFFFF) {
        uint32_t d32 = (uint32_t)d;
        if ((d32 & (d32 - 1)) == 0) {
            return n & (d - 1);
        }
    }

    /* General case: binary long division, return remainder */
    uint64_t r = 0;

    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
        }
    }

    return r;
}

/* Combined divmod for efficiency */
uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem) {
    uint64_t q = __udivdi3(n, d);
    if (rem) *rem = n - q * d;
    return q;
}
