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

    float phase_error_history[160]; // Окно анализа стабильности (20 мс)
    int   lock_idx;
    int   lock_ready;               // Флаг заполнения буфера истории
    // Дополнительные переменные для мгновенного расчета дисперсии
    float error_sum;         // Сумма элементов окна
    float error_sq_sum;      // Сумма квадратов элементов окна

    // Анализатор стабильности частоты (20 мс)
    float freq_history[160];
    float freq_sum;
    float freq_sq_sum;
    int   freq_idx;
    int   freq_ready;
} pll_tracker_t;

// Инициализация ФАПЧ.
// coarse_bin — номер бина, который поймало наше 256-БПФ (например, 32 или 33)
void pll_tracker_init(pll_tracker_t *pll, int coarse_bin, short *sine_table_ptr);

// Боевой шаг петли: вызывается на КАЖДЫЙ входящий отсчет АЦП (8000 раз в сек)
// Вход: зашумленный отсчет из эфира. Выход: очищенный от сдвига частоты отсчет
void pll_tracker_tick(pll_tracker_t *pll, const cplx_f32 *in_sample, cplx_f32 *out_sample);

int pll_is_captured(const pll_tracker_t *pll);
void pll_tracker_set_mode(pll_tracker_t *pll, int aggressive_mode);

#endif // CARRIER_RECOVERY_H
