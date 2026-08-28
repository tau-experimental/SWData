#include "complex_math.h"
#include <math.h> /* Только для sqrtf */

cplx_f32 cplx_set(float re, float im) {
    cplx_f32 res;
    res.re = re;
    res.im = im;
    return res;
}

cplx_f32 cplx_add(cplx_f32 a, cplx_f32 b) {
    cplx_f32 res;
    res.re = a.re + b.re;
    res.im = a.im + b.im;
    return res;
}

cplx_f32 cplx_sub(cplx_f32 a, cplx_f32 b) {
    cplx_f32 res;
    res.re = a.re - b.re;
    res.im = a.im - b.im;
    return res;
}

cplx_f32 cplx_mul(cplx_f32 a, cplx_f32 b) {
    cplx_f32 res;
    res.re = (a.re * b.re) - (a.im * b.im);
    res.im = (a.re * b.im) + (a.im * b.re);
    return res;
}

cplx_f32 cplx_mul_conj(cplx_f32 a, cplx_f32 b) {
    cplx_f32 res;
    res.re = (a.re * b.re) + (a.im * b.im);
    res.im = (a.im * b.re) - (a.re * b.im);
    return res;
}

cplx_f32 cplx_conj(cplx_f32 in) {
    cplx_f32 res;
    res.re = in.re;
    res.im = -in.im;
    return res;
}

float cplx_mag_sq(cplx_f32 in) {
    return (in.re * in.re) + (in.im * in.im);
}

float cplx_mag(cplx_f32 in) {
    return sqrtf((in.re * in.re) + (in.im * in.im));
}
