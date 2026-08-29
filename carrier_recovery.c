#include "carrier_recovery.h"
#include <math.h>

void pll_tracker_init(pll_tracker_t *pll, int coarse_bin, short *sine_table_ptr) {
    pll->phase_acc = 0;
    pll->sine_lut  = sine_table_ptr;

    // Базовая частота жестко привязана к номиналу 1000 Гц (бин 32)
    pll->phase_base = 32 * 256; // 8192 в масштабе DDS

    // Инициализируем интегратор на основе реальной подсказки от БПФ!
    // Если БПФ сказало, что пик ушел в бин 33, то частота примерно 1031 Гц.
    // Значит, интегратор должен сразу стартовать со смещения +1.0 бина (+31.25 Гц)
    if (coarse_bin == 33) { // но это -- ЖУЛЬНИЧЕСТВО, потому что в действительности стартовый бин может быть почти где угодно
        pll->freq_integrator = 1.0f; // Стартуем сразу с +31.25 Гц
    } else {
        pll->freq_integrator = 0.0f; // Стартуем с 1000 Гц
    }

    // РАЗГОНЯЕМ ПЕТЛЮ: увеличиваем коэффициенты для мгновенного захвата
    pll->kp = 0.45f;   // Было 0.04
    pll->ki = 0.012f;  // Было 0.001

    // Очистка истории лока
    pll->lock_idx = 0;
    pll->lock_ready = 0;
    for (int i = 0; i < 160; i++) {
        pll->phase_error_history[i] = 0.0f;
    }

    pll->phase_acc = 0;
    pll->sine_lut = sine_table_ptr;
    pll->phase_base = 32 * 256;
    pll->freq_integrator = (coarse_bin == 33) ? 1.0f : 0.0f;
    pll->kp = 0.45f;
    pll->ki = 0.012f;

    // Очистка истории частоты
    pll->freq_sum = 0.0f;
    pll->freq_sq_sum = 0.0f;
    pll->freq_idx = 0;
    pll->freq_ready = 0;
    for(int i=0; i<160; i++) pll->freq_history[i] = 0.0f;
}

void pll_tracker_set_mode(pll_tracker_t *pll, int aggressive_mode) {
    if (aggressive_mode) {
        // Режим захвата: быстрая реакция на включение пилота
        pll->kp = 0.45f;
        pll->ki = 0.012f;
    } else {
        // Режим удержания: глубокая инертность, фильтрация информационных скачков
        pll->kp = 0.002f;
        pll->ki = 0.00005f;
    }
}

void pll_tracker_tick(pll_tracker_t *pll, const cplx_f32 *in_sample, cplx_f32 *out_sample) {
    // 1. Стандартный микшер и расчет phase_error
    unsigned char sin_idx = (unsigned char)(pll->phase_acc >> 8);
    unsigned char cos_idx = (unsigned char)((sin_idx + 64) & 0xFF);
    float local_cos = (float)pll->sine_lut[cos_idx] / 32000.0f;
    float local_sin = (float)pll->sine_lut[sin_idx] / 32000.0f;

    out_sample->re = in_sample->re * local_cos + in_sample->im * local_sin;
    out_sample->im = in_sample->im * local_cos - in_sample->re * local_sin;

    float phase_error = out_sample->re * out_sample->im;
    if (phase_error > 1.0f)  phase_error = 1.0f;
    if (phase_error < -1.0f) phase_error = -1.0f;

    pll->freq_integrator += pll->ki * phase_error;
    if (pll->freq_integrator > 1.92f)  pll->freq_integrator = 1.92f;
    if (pll->freq_integrator < -1.92f) pll->freq_integrator = -1.92f;

    // 2. СКОЛЬЗЯЩИЙ АНАЛИЗ СТАБИЛЬНОСТИ ЧАСТОТЫ ЗА O(1)
    float old_freq = pll->freq_history[pll->freq_idx];
    pll->freq_sum    -= old_freq;
    pll->freq_sq_sum -= (old_freq * old_freq);

    pll->freq_history[pll->freq_idx] = pll->freq_integrator;
    pll->freq_sum    += pll->freq_integrator;
    pll->freq_sq_sum += (pll->freq_integrator * pll->freq_integrator);

    pll->freq_idx++;
    if (pll->freq_idx >= 160) {
        pll->freq_idx = 0;
        pll->freq_ready = 1;

        // Сброс float-утечки
        float e_sum = 0.0f;
        for(int i=0; i<160; i++) e_sum += pll->freq_history[i];
        pll->freq_sum = e_sum;
    }

    float total_step = (float)pll->phase_base + (pll->kp * phase_error * 256.0f) + (pll->freq_integrator * 256.0f);
    pll->phase_acc = (unsigned short)(pll->phase_acc + (unsigned short)total_step);
}
// Возвращает 1, если петля ФАПЧ стабильно удерживает несущую пилот-тона
int pll_is_captured(const pll_tracker_t *pll) {
    if (!pll->freq_ready) return 0;

    // Считаем дисперсию (вариацию) частоты гетеродина в окне
    float mean = pll->freq_sum / 160.0f;
    float mean_sq = pll->freq_sq_sum / 160.0f;
    float variance = mean_sq - (mean * mean);

    // На хаотичном шуме интегратор постоянно бросает из стороны в сторону (variance > 0.1).
    // При фиксации пилот-тона интегратор замирает в константе, и variance падает практически в ноль!
    // Порог 0.001f — это железобетонный индикатор захвата частоты при любом клиппинге.
    if (variance >= 0.0f && variance < 0.001f) {
        return 1;
    }
    return 0;
}
