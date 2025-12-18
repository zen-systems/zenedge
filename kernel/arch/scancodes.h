#ifndef _SCANCODES_H
#define _SCANCODES_H

/* US QWERTY Scancode Set 1 */
static const char scancode_map[] = {
    0,    27,   '1', '2',  '3', '4',  '5',  '6',
    '7',  '8',  '9', '0',  '-', '=',  '\b', /* 0x00 - 0x0E */
    '\t', 'q',  'w', 'e',  'r', 't',  'y',  'u',
    'i',  'o',  'p', '[',  ']', '\n', /* 0x0F - 0x1C */
    0,    'a',  's', 'd',  'f', 'g',  'h',  'j',
    'k',  'l',  ';', '\'', '`', /* 0x1D - 0x29 */
    0,    '\\', 'z', 'x',  'c', 'v',  'b',  'n',
    'm',  ',',  '.', '/',  0, /* 0x2A - 0x36 */
    '*',  0,    ' ', 0,       /* 0x37 - 0x3A */
                              /* ... incomplete map for brevity ... */
};

static const char scancode_map_shift[] = {
    0,   27,  '!',  '@',  '#',  '$', '%', '^', '&', '*', '(', ')',
    '_', '+', '\b', '\t', 'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{',  '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H',
    'J', 'K', 'L',  ':',  '"',  '~', 0,   '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M',  '<',  '>',  '?', 0,   '*', 0,   ' ', 0,
};

#endif /* _SCANCODES_H */
