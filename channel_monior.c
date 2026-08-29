#include "channel_monitor.h"
#include <math.h>

float channel_monitor_process(channel_monitor_t *mon, const cplx_f32 *raw_sample) {
    float power = raw_sample->re * raw_sample->re + raw_sample->im * raw_sample->im;

    mon->running_sum -= mon->delay_line[mon->idx];
    mon->delay_line[mon->idx] = power;
    mon->running_sum += power;

    mon->idx++;
    if (mon->idx >= 160) {
        mon->idx = 0;

        // Сброс ошибки float
        float exact_sum = 0.0f;
        for (int i = 0; i < 160; i++) exact_sum += mon->delay_line[i];
        mon->running_sum = exact_sum;

        if (!mon->ready) {
            mon->noise_floor = mon->running_sum / 160.0f;
            mon->ready = 1;
        }
    }

    if (!mon->ready) return 0.0f;

    float current_rssi = mon->running_sum / 160.0f;

    // Считаем SNR в децибелах: 10 * log10( Сигнал_вместе_с_шумом / Чистый_Шум )
    // Если сигнала нет, отношение близко к 1.0, log10(1) = 0 дБ.
    if (current_rssi < mon->noise_floor) return 0.0f;

    float snr_db = 10.0f * log10f(current_rssi / (mon->noise_floor + 1e-6f));
    return snr_db;
}
