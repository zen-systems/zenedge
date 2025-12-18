/* kernel/console.h */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stddef.h>
#include <stdint.h>

void console_write(const char *str);
void console_putc(char c);
void console_cls(void);
void print_hex32(uint32_t val);
void print_uint(uint32_t val);

#endif /* _CONSOLE_H */
