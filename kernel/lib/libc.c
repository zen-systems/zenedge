#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "../console.h"

/* Minimal implementations for WASM3 */

void abort(void) {
    console_write("[libc] ABORT!\n");
    while(1) { __asm__("hlt"); }
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

int abs(int j) {
    return (j < 0) ? -j : j;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    unsigned long result = 0;
    const char *p = nptr;
    /* Skip whitespace? Minimal implementation doesn't handle space/sign well unless needed */
    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') digit = *p - 'A' + 10;
        else break;
        
        if (digit >= base) break;
        result = result * base + digit;
        p++;
    }
    if (endptr) *endptr = (char *)p;
    return result;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)strtoul(nptr, endptr, base);
}

/* printf stub - minimal support */
/* We won't implement full formatting yet, just pass string or hex */
int printf(const char *format, ...) {
    console_write("[wasm_log] ");
    console_write(format); /* DANGEROUS: format might have % */
    /* TODO: Proper vsnprintf */
    return 0;
}

int sprintf(char *str, const char *format, ...) {
    /* Stub: empty string */
    if (str) *str = 0;
    return 0;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    if (str && size > 0) *str = 0;
    return 0;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (str && size > 0) *str = 0;
    return 0;
}

int vprintf(const char *format, va_list ap) {
    console_write(format);
    return 0;
}

int putchar(int c) {
    /* console_putc(c); */
    return c;
}

/* Math Stubs if needed despite floats disabled */
/*
double floor(double x) { return (int)x; }
double ceil(double x) { return (int)x + 1; }
double sqrt(double x) { return x; } 
*/
