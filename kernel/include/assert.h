#ifndef _ASSERT_H
#define _ASSERT_H

#include <stdlib.h>
#include <stdio.h>

#define assert(expression) ((void)((expression) ? 0 : (printf("[assert] %s:%d: failed assertion '%s'\n", __FILE__, __LINE__, #expression), abort(), 0)))

#endif
