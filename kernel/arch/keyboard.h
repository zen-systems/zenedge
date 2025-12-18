#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include "idt.h" /* For interrupt_frame_t */

void keyboard_init(void);
void keyboard_handler(interrupt_frame_t *regs);

/* Blocking read character */
char tc_getchar(void);

/* Check if character is available */
int tc_has_input(void);

#endif /* _KEYBOARD_H */
