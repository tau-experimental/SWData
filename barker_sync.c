#include "barker_sync.h"
#include <math.h>

void barker_sync_init(barker_sync_t *sync) {
    sync->current_integrator.re = 0.0f;
    sync->current_integrator.im = 0.0f;
    sync->sample_counter = 0;

    for (int i = 0; i < 6; i++) {
        sync->symbol_history[i].re = 0.0f;
        sync->symbol_history[i].im = 0.0f;
    }

    // Идеальные комплексные точки для дибитов Баркера (\pi/4-DQPSK)
    // Символ 0 (11) -> -3pi/4 (-135°) -> re=-0.707, im=-0.707
    // Символ 1 (10) -> -pi/4  (-45°)  -> re= 0.707, im=-0.707
    // Символ 2 (00) -> +pi/4  (+45°)  -> re= 0.707, im= 0.707
    // Символ 3 (10) -> -pi/4  (-45°)  -> re= 0.707, im=-0.707
    // Символ 4 (01) -> +3pi/4 (+135°) -> re=-0.707, im= 0.707
    // Символ 5 (00) -> +pi/4  (+45°)  -> re= 0.707, im= 0.707

    double angles[6] = { -3.0*M_PI/4.0, -M_PI/4.0, M_PI/4.0, -M_PI/4.0, 3.0*M_PI/4.0, M_PI/4.0 };
    for (int i = 0; i < 6; i++) {
        sync->template_barker[i].re = (float)cos(angles[i]);
        sync->template_barker[i].im = (float)sin(angles[i]);
    }
}

int barker_sync_tick(barker_sync_t *sync, const cplx_f32 *pll_output_sample, float *out_corr_power) {
    // 1. Интегрируем (копим энергию символа, вычищая КВ-шум)
    sync->current_integrator.re += pll_output_sample->re;
    sync->current_integrator.im += pll_output_sample->im;
    sync->sample_counter++;

    // Ждем окончания символа (800 отсчетов при 10 Бод)
    if (sync->sample_counter < 800) {
        *out_corr_power = 0.0f;
        return 0; // Символ еще не завершен
    }

    // --- СИМВОЛ ЗАВЕРШЕН (Integrate and Dump) ---
    // Нормализуем накопленный вектор символа
    cplx_f32 finished_symbol;
    finished_symbol.re = sync->current_integrator.re / 800.0f;
    finished_symbol.im = sync->current_integrator.im / 800.0f;

    // Сбрасываем интегратор для следующего символа
    sync->current_integrator.re = 0.0f;
    sync->current_integrator.im = 0.0f;
    sync->sample_counter = 0;

    // Сдвигаем историю символов в кольцевом буфере (прокрутка времени)
    for (int i = 0; i < 5; i++) {
        sync->symbol_history[i] = sync->symbol_history[i + 1];
    }
    sync->symbol_history[5] = finished_symbol;

    // 2. Взаимная комплексная корреляция со статической маской Баркера
    // Сумма (History[i] * conj(Template[i]))
    cplx_f32 corr_sum = {0.0f, 0.0f};
    for (int i = 0; i < 6; i++) {
        float hr = sync->symbol_history[i].re;
        float hi = sync->symbol_history[i].im;
        float tr = sync->template_barker[i].re;
        float ti = sync->template_barker[i].im;

        // Комплексное умножение на сопряженное: (hr + j*hi) * (tr - j*ti)
        corr_sum.re += (hr * tr + hi * ti);
        corr_sum.im += (hi * tr - hr * ti);
    }

    // Считаем мощность корреляционного пика
    float power = corr_sum.re * corr_sum.re + corr_sum.im * corr_sum.im;
    *out_corr_power = power;

    // Порог обнаружения. В чистом поле максимальное значение мощности равно ~1.0.
    // С учетом КВ-шума 6 дБ, выставим жесткий, но уверенный порог в 0.35.
    if (power > 0.35f) {
        return 1; // УСПЕХ: Временная засечка найдена!
    }

    return 0;
}
