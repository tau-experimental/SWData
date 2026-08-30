#include "channel_sim.h"
#include <math.h>
#include <stdlib.h>

void channel_sim_init(qshort_channel_sim_t *sim, double snr_db, double freq_offset_hz, double sample_rate) {
    sim->freq_offset = freq_offset_hz;
    sim->sample_rate = sample_rate;
    sim->phase_acc = 0;

    // Пересчет SNR (дБ) в амплитуду шума (Sigma)
    // Так как амплитуда нашего идеального сигнала во float нормализована к ~0.707 (RMS),
    // то мощность сигнала P_sig = 0.5.
    // SNR = 10 * log10(P_sig / P_noise) -> P_noise = P_sig / (10^(SNR/10))
    // Sigma = sqrt(P_noise)
    double snr_linear = pow(10.0, snr_db / 10.0);
    double power_noise = 0.5 / snr_linear;
    sim->noise_sigma = sqrt(power_noise);

    printf ("Эмулятор радиоканала готов к работе, SNR %+4.1f, начальный частотный сдвиг %+4.1f\n", snr_db, freq_offset_hz);
}

// Вспомогательный генератор Гауссова шума (Бокс-Мюллер)
static double generate_gauss(double sigma) {
    double u1 = (double)rand() / RAND_MAX;
    double u2 = (double)rand() / RAND_MAX;
    if (u1 < 1e-9) u1 = 1e-9; // Защита от log(0)

    // Возвращаем одно число с нормальным распределением
    return sigma * sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

void channel_sim_process(qshort_channel_sim_t *sim, const cplx_f32 *in_sample, cplx_f32 *out_sample) {
    // --- 1. Имитация дрейфа частоты (Гетеродинирование / Поворот фазы) ---
    // Вычисляем текущий угол поворота фазы помехи: 2 * pi * f_offset * t
    double angle = (2.0 * M_PI * sim->freq_offset * sim->phase_acc) / sim->sample_rate;

    // Инкрементируем время (шаг аккумулятора)
    sim->phase_acc++;

    double cos_a = cos(angle);
    double sin_a = sin(angle);

    // Умножаем комплексный отсчет сигнала на комплексный экспоненциальный сдвиг частоты ejw
    // (I + jQ) * (cos + jsin) = (I*cos - Q*sin) + j(I*sin + Q*cos)
    double rotated_i = in_sample->re * cos_a - in_sample->im * sin_a;
    double rotated_q = in_sample->re * sin_a + in_sample->im * cos_a;

    // --- 2. Подмешивание аддитивного белого гауссова шума (AWGN) ---
    // Шум добавляется независимо в синфазный (I) и квадратурный (Q) каналы
    out_sample->re = (float)(rotated_i + generate_gauss(sim->noise_sigma));
    out_sample->im = (float)(rotated_q + generate_gauss(sim->noise_sigma));
}
