#ifndef CARRIER_RECOVERY_H
#define CARRIER_RECOVERY_H

#include "wav_io.h"
#include <stdint.h>

typedef struct {
    // Состояние локального DDS-гетеродина приемника
    unsigned short phase_acc;   // 16-битный аккумулятор фазы
    unsigned short phase_base;  // Базовая частота (1000 Гц в терминах FTW = 8192)
    short          *sine_lut;   // Указатель на ту же таблицу синуса из модулятора

    // Коэффициенты и интеграторы петли ФАПЧ
    float kp;                   // Пропорциональный коэффициент
    float ki;                   // Интегральный коэффициент
    float freq_integrator;      // Накопленная ошибка частоты (интегратор петли)
} pll_tracker_t;

// Инициализация ФАПЧ.
// coarse_bin — номер бина, который поймало наше 256-БПФ (например, 32 или 33)
void pll_tracker_init(pll_tracker_t *pll, int coarse_bin, short *sine_table_ptr);

// Боевой шаг петли: вызывается на КАЖДЫЙ входящий отсчет АЦП (8000 раз в сек)
// Вход: зашумленный отсчет из эфира. Выход: очищенный от сдвига частоты отсчет
void pll_tracker_tick(pll_tracker_t *pll, const cplx_f32 *in_sample, cplx_f32 *out_sample);

#endif // CARRIER_RECOVERY_H
