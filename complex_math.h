#ifndef COMPLEX_MATH_H
#define COMPLEX_MATH_H
#include <stdint.h>

/* Комплексное число с плавающей точкой (для вычислений повышенной точности с широкой динамикой) */
typedef struct {
    float re, im;
} cplx_f32;

/* Комплексное число с фиксированной точкой: Q15 или целочисленный отсчет АЦП */
typedef struct {
    uint16_t re, im;
} cplx_i16;

/* Комплексное число с фиксированной точкой: Q31 */
typedef struct {
	int32_t re, im;
} cplx_i32;

/* Инициализация комплексного числа */
cplx_f32 cplx_set(float re, float im);

/* Сложение: out = a + b */
cplx_f32 cplx_add(cplx_f32 a, cplx_f32 b);

/* Вычитание: out = a - b */
cplx_f32 cplx_sub(cplx_f32 a, cplx_f32 b);

/* Умножение: out = a * b */
cplx_f32 cplx_mul(cplx_f32 a, cplx_f32 b);

/* Умножение на комплексно-сопряженное: out = a * conj(b) */
/* Критически важно для дифференциального демодулятора */
cplx_f32 cplx_mul_conj(cplx_f32 a, cplx_f32 b);

/* Комплексно-сопряженное число: out = conj(in) */
cplx_f32 cplx_conj(cplx_f32 in);

/* Квадрат модуля (мощность): out = re^2 + im^2 */
float cplx_mag_sq(cplx_f32 in);
/* фаза */
float cplx_phase(cplx_f32 in);

/* Модуль (амплитуда): out = sqrt(re^2 + im^2) */
/* Использует аппаратный корень на ПК и аппаратный fsqrt.s на CH32V307 */
float cplx_mag(cplx_f32 in);

extern cplx_i32 cplx_i32_mul_conj(cplx_i16 a, cplx_i16 b);

#endif /* COMPLEX_MATH_H */
