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

#define INFINITY (1.0/0.0)
#define NAN (0.0/0.0)

int isnan(double x);
int isinf(double x);

#endif
