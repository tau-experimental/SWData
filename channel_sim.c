#include "channel_sim.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// Постоянная Пи, если её нет в math.h
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void channel_sim_init(qshort_channel_sim_t *sim,
                      double snr_db,
                      double freq_offset_hz,
                      double freq_drift_hz_per_sec,
                      double fade_freq_hz,
                      double sample_rate)
{
    sim->freq_offset = freq_offset_hz;
    sim->freq_drift_rate = freq_drift_hz_per_sec;
    sim->sample_rate = sample_rate;
    sim->phase_acc = 0.0; // Храним текущую фазу прямо в радианах

    // 1. Расчет Sigma для AWGN шума
    double snr_linear = pow(10.0, snr_db / 10.0);
    double power_noise = 0.5 / snr_linear;
    sim->noise_sigma = sqrt(power_noise);

    // 2. Инициализация замираний (Полярный флаттер)
    if (fade_freq_hz > 0.0) {
        sim->fade_enabled = 1;
        sim->fade_freq = fade_freq_hz;
        // Шаг фазы LFO за один отсчет
        sim->fade_inc = (2.0 * M_PI * fade_freq_hz) / sample_rate;
        // Задаем случайные начальные фазы для ионосферных лучей, чтобы I и Q замираний были ортогональны
        sim->fade_phase_i = (double)rand() / RAND_MAX * 2.0 * M_PI;
        sim->fade_phase_q = (double)rand() / RAND_MAX * 2.0 * M_PI;
    } else {
        sim->fade_enabled = 0;
        sim->fade_freq = 0.0;
        sim->fade_inc = 0.0;
    }
    srand(time());

    printf("КВ-Симулятор готов: SNR=%+4.1f дБ, F_off=%+4.1f Гц, Дрейф=%+4.2f Гц/с, Флаттер=%+4.2f Гц\n",
           snr_db, freq_offset_hz, freq_drift_hz_per_sec, fade_freq_hz);
}

static double generate_gauss(double sigma) {
    double u1 = (double)rand() / RAND_MAX;
    double u2 = (double)rand() / RAND_MAX;
    if (u1 < 1e-9) u1 = 1e-9;
    return sigma * sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

void channel_sim_process(qshort_channel_sim_t *sim, const cplx_f32 *in_sample, cplx_f32 *out_sample) {

    double sig_i = in_sample->re;
    double sig_q = in_sample->im;

    // --- 1. Настоящие комплексные замирания Рэлея (Метод Райса) ---
    if (sim->fade_enabled) {
        // Формируем комплексный коэффициент замирания H = h_i + j * h_q
        // Используем комбинацию ортогональных LFO для полной независимости каналов
        double h_i = cos(sim->fade_phase_i);
        double h_q = sin(sim->fade_phase_q); // Строго sin и cos от разных аргументов!

        // Комплексное перемножение: Сигнал * H
        // (sig_i + j*sig_q) * (h_i + j*h_q) = (sig_i*h_i - sig_q*h_q) + j*(sig_i*h_q + sig_q*h_i)
        double faded_i = sig_i * h_i - sig_q * h_q;
        double faded_q = sig_i * h_q + sig_q * h_i;

        // Нормировка энергии
        sig_i = faded_i * 0.707;
        sig_q = faded_q * 0.707;

        // Шаг фаз LFO замираний с некратными (иррациональными) скоростями
        // Это заставит комплексное созвездие канала описывать хаотичную траекторию (аттрактор)
        sim->fade_phase_i += sim->fade_inc;
        sim->fade_phase_q += sim->fade_inc * 1.2345; // Разносим частоты, чтобы сломать синхронность огибающих

        if (sim->fade_phase_i >= 2.0 * M_PI) sim->fade_phase_i -= 2.0 * M_PI;
        if (sim->fade_phase_q >= 2.0 * M_PI) sim->fade_phase_q -= 2.0 * M_PI;
    }

    // --- 2. Гетеродинирование (Сдвиг частоты и Дрейф) ---
    // Вращаем спектр с помощью нашего основного аккумулятора фазы
    double cos_a = cos(sim->phase_acc);
    double sin_a = sin(sim->phase_acc);

    double rotated_i = sig_i * cos_a - sig_q * sin_a;
    double rotated_q = sig_i * sin_a + sig_q * cos_a;

    // Шаг фазы сдвига частоты
    double phase_inc = (2.0 * M_PI * sim->freq_offset) / sim->sample_rate;
    sim->phase_acc += phase_inc;

    if (sim->phase_acc >= 2.0 * M_PI) sim->phase_acc -= 2.0 * M_PI;
    if (sim->phase_acc < 0.0) sim->phase_acc += 2.0 * M_PI;

    // Линейный уход частоты вверх/вниз (термический дрейф)
    sim->freq_offset += sim->freq_drift_rate / sim->sample_rate;

    // --- 3. Аддитивный белый шум ---
    out_sample->re = (float)(rotated_i + generate_gauss(sim->noise_sigma));
    out_sample->im = (float)(rotated_q + generate_gauss(sim->noise_sigma));
}

