#ifndef DSP_UTILS_H
#define DSP_UTILS_H
#include <stdbool.h>
#include <inttypes.h>
#define M_PI_F 3.14159265358979323846f

/* Комплексное число одинарной точности (float для FPU) */
typedef struct {
    float re;
    float im;
} complex_f;

/* Структура экспоненциального сглаживающего фильтра "бета*S[s] + (1-бета)*S[n-1]" */
typedef struct {
    float state;
    float beta;
} dsp_ema_filter_t;

/* Базовая комплексная математика */
static inline complex_f c_add(complex_f a, complex_f b) {
    complex_f res = {a.re + b.re, a.im + b.im};
    return res;
}

static inline complex_f c_sub(complex_f a, complex_f b) {
    complex_f res = {a.re - b.re, a.im - b.im};
    return res;
}

static inline complex_f c_mul(complex_f a, complex_f b) {
    complex_f res;
    res.re = a.re * b.re - a.im * b.im;
    res.im = a.re * b.im + a.im * b.re;
    return res;
}

static inline float c_mag2(complex_f a) {
    return a.re * a.re + a.im * a.im;
}

static inline float generate_white_noise(void) {
    static uint32_t next = 1;
    next = next * 1103515245 + 12345;
    float rand_val = (float)(next / 65536 % 32768) / 32767.0f; // [0.0, 1.0]
    return (rand_val * 2.0f) - 1.0f; // Переводим в диапазон [-1.0, 1.0]
}

static inline void dsp_ema_init(dsp_ema_filter_t *f, float beta) {
    f->state = 0.0f;
    f->beta = beta;
}

static inline float dsp_ema_process(dsp_ema_filter_t *f, float input) {
    f->state = f->beta * input + (1.0f - f->beta) * f->state;
    return f->state;
}

#endif /* DSP_UTILS_H */
