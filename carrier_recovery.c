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
    if (coarse_bin == 33) {
        pll->freq_integrator = 1.0f; // Стартуем сразу с +31.25 Гц
    } else {
        pll->freq_integrator = 0.0f; // Стартуем с 1000 Гц
    }

    // РАЗГОНЯЕМ ПЕТЛЮ: увеличиваем коэффициенты для мгновенного захвата
    pll->kp = 0.45f;   // Было 0.04
    pll->ki = 0.012f;  // Было 0.001
}

void pll_tracker_tick(pll_tracker_t *pll, const cplx_f32 *in_sample, cplx_f32 *out_sample) {
    unsigned char sin_idx = (unsigned char)(pll->phase_acc >> 8);
    unsigned char cos_idx = (unsigned char)((sin_idx + 64) & 0xFF);

    float local_cos = (float)pll->sine_lut[cos_idx] / 32000.0f;
    float local_sin = (float)pll->sine_lut[sin_idx] / 32000.0f;

    // Комплексный микшер
    out_sample->re = in_sample->re * local_cos + in_sample->im * local_sin;
    out_sample->im = in_sample->im * local_cos - in_sample->re * local_sin;

    // Дискриминатор фазовой ошибки
    float phase_error = out_sample->re * out_sample->im;

    if (phase_error > 1.0f)  phase_error = 1.0f;
    if (phase_error < -1.0f) phase_error = -1.0f;

    // Обновляем интегратор частоты
    pll->freq_integrator += pll->ki * phase_error;

    // Ограничиваем полет интегратора рамками нашего допуска \pm 60 Гц
    // (\pm 60 Гц / 31.25 Гц = \pm 1.92 бина)
    if (pll->freq_integrator > 1.92f)  pll->freq_integrator = 1.92f;
    if (pll->freq_integrator < -1.92f) pll->freq_integrator = -1.92f;

    // Расчет шага фазы DDS
    float total_step = (float)pll->phase_base + (pll->kp * phase_error * 256.0f) + (pll->freq_integrator * 256.0f);

    pll->phase_acc = (unsigned short)(pll->phase_acc + (unsigned short)total_step);
}
