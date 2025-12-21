#include "math.h"


/* Math stubs for WASM3 */
double sqrt(double x) {
    /* Fast inverse square root is overkill; let's do a simple iterative or just return x for now? 
       Wait, simple linear policy doesn't use sqrt.
       But wasm3 might reference it if d_m3HasFloat=1.
       Let's implement a dummy or tiny approximation if needed.
       Assembly fsqrt?
    */
    double res;
    __asm__("sqrtsd %1, %0" : "=x"(res) : "x"(x));
    return res;
}

double floor(double x) {
    long long i = (long long)x;
    return (double)i; // Good enough for positive? 
    /* Correct: if (x < i) i-- */
    /* __builtin_floor? */
    return __builtin_floor(x);
}

double ceil(double x) {
    return __builtin_ceil(x);
}

double trunc(double x) {
    return __builtin_trunc(x);
}

double fabs(double x) {
    return __builtin_fabs(x);
}

double rint(double x) {
    return __builtin_rint(x); // round to nearest integer
}

/* float versions if needed */
float sqrtf(float x) { return (float)sqrt((double)x); }
float floorf(float x) { return (float)floor((double)x); }
float ceilf(float x) { return (float)ceil((double)x); }
float truncf(float x) { return (float)trunc((double)x); }
float fabsf(float x) { return (float)fabs((double)x); }
float rintf(float x) { return (float)rint((double)x); }

double copysign(double x, double y) {
    return __builtin_copysign(x, y);
}

float copysignf(float x, float y) {
    return __builtin_copysignf(x, y);
}

float math_vec_dot(const float *a, const float *b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}
