#ifndef COMPLEX_MATH_H
#define COMPLEX_MATH_H

/* Комплексное число с плавающей точкой (для ПК-модели высокой точности) */
typedef struct {
    float re;
    float im;
} cplx_f32;

/* Комплексное число с фиксированной точкой (для будущего переноса на CH32V307) */
typedef struct {
    short re; /* Q15 или целочисленный отсчет АЦП */
    short im;
} cplx_i16;

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

/* Модуль (амплитуда): out = sqrt(re^2 + im^2) */
/* Использует аппаратный корень на ПК и аппаратный fsqrt.s на CH32V307 */
float cplx_mag(cplx_f32 in);

#endif /* COMPLEX_MATH_H */
