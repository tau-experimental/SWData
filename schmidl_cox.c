#include "schmidl_cox.h"
#include <string.h>

void sc_init(schmidl_cox_t *sc) {
    sc->ptr = 0;
    sc->running_sum.re = 0.0f;
    sc->running_sum.im = 0.0f;
    sc->energy_b = 0.0f;
    memset(sc->delay_line, 0, sizeof(sc->delay_line));
}

void sc_tick(schmidl_cox_t *sc, const cplx_f32 *input_sample, float *out_power, cplx_f32 *out_complex_corr) {
    // 1. Вычисляем индекс центральной точки (задержка 400 сэмплов)
    int center_idx = sc->ptr + SC_HALF_LEN;
    if (center_idx >= SC_SYM_LEN) {
        center_idx -= SC_SYM_LEN;
    }

    // 2. Извлекаем три ключевые точки из FIFO
    cplx_f32 head   = *input_sample;
    cplx_f32 center = sc->delay_line[center_idx];
    cplx_f32 tail   = sc->delay_line[sc->ptr]; // Старый сэмпл в этой ячейке и есть хвост (задержка 800)

    // 3. Записываем свежую Голову в буфер (продвигаем FIFO)
    sc->delay_line[sc->ptr] = head;

    // 4. Мгновенные произведения для дифференциального шага CIC-фильтра
    // Новое произведение на входе окна (Голова * conj(Центр))
    cplx_f32 d_curr;
    d_curr.re = head.re * center.re + head.im * center.im;
    d_curr.im = head.im * center.re - head.re * center.im;

    // Старое произведение, уходящее из окна (Центр * conj(Хвост))
    cplx_f32 d_old;
    d_old.re = center.re * tail.re + center.im * tail.im;
    d_old.im = center.im * tail.re - center.re * tail.im;

    // 5. Скользящее обновление CIC-аккумулятора корреляции
    sc->running_sum.re += (d_curr.re - d_old.re);
    sc->running_sum.im += (d_curr.im - d_old.im);

    // 6. Скользящее обновление энергии правого окна (Окна B: от Центра до Хвоста)
    // Энергия входящего в окно B сэмпла (center) минус энергия уходящего (tail)
    float p_center = center.re * center.re + center.im * center.im;
    float p_tail   = tail.re * tail.re + tail.im * tail.im;
    sc->energy_b  += (p_center - p_tail);

    // Сдвигаем указатель кольцевого буфера
    sc->ptr++;
    if (sc->ptr >= SC_SYM_LEN) {
        sc->ptr = 0;
    }

    // Отдаем комплексную корреляцию наружу (понадобится для узора)
    *out_complex_corr = sc->running_sum;

    // 7. Расчет нормированной мощности Шмидля-Кокса
    float abs_corr_sq = sc->running_sum.re * sc->running_sum.re + sc->running_sum.im * sc->running_sum.im;

    // Защита от деления на ноль в тишине
    if (sc->energy_b < 0.01f) {
        *out_power = 0.0f;
        return;
    }

    // По канону Шмидля-Кокса: P = |R|^2 / (P_b)^2
    // Но так как у нас окна по 400 сэмплов, нормируем на квадрат энергии
    *out_power = abs_corr_sq / (sc->energy_b * sc->energy_b);

    if (*out_power > 1.0f) *out_power = 1.0f;
}
