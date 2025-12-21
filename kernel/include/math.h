#ifndef _MATH_H
#define _MATH_H

/* math.h for wasm3 */
/* We have hardware FPU enabled. */
/* wasm3 needs basic math funcs. */

double floor(double x);
double ceil(double x);
double trunc(double x);
double fabs(double x);
double sqrt(double x);
double pow(double x, double y);
double fmin(double x, double y);
double fmax(double x, double y);
double rint(double x);
double copysign(double x, double y);

float floorf(float x);
float ceilf(float x);
float truncf(float x);
float fabsf(float x);
float sqrtf(float x);
float rintf(float x);
float copysignf(float x, float y);

/* signbit macro or function? WASM3 uses it like a function often, but standard is macro. */
/* Let's double check if we can macro it. */
#define signbit(x) __builtin_signbit(x)

#define INFINITY (1.0/0.0)
#define NAN (0.0/0.0)

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)

#endif
