/* kernel/arch/syscall.h */

#ifndef _ARCH_SYSCALL_H
#define _ARCH_SYSCALL_H

#include "idt.h"
#include <stdint.h>

/* Initialize syscall subsystem */
void syscall_init(void);

/* The handler (registered in IDT) */
void syscall_handler(interrupt_frame_t *frame);

#endif /* _ARCH_SYSCALL_H */
