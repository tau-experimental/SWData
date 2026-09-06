#ifndef CHANNEL_SIM_H
#define CHANNEL_SIM_H

#include "wav_io.h"

typedef struct {
    double noise_sigma;        // Среднеквадратичное отклонение шума (задается через SNR)
    double freq_offset;        // Текущий сдвиг частоты в Герцах (например, +50 Гц)
    double freq_drift_rate;    // Скорость дрейфа частоты в Гц/сек (например, +0.2 Гц/сек)
    double sample_rate;        // Частота дискретизации (8000 Гц)

    // Аккумуляторы фазы
    double phase_acc;          // Глобальная фаза постоянного сдвига частоты (в радианах)

    // Модель КВ-замираний (Полярный флаттер / Уоттерсон)
    int fade_enabled;          // Флаг включения замираний (1 - вкл, 0 - выкл)
    double fade_freq;          // Частота замираний в Гц (например, 0.5 Гц для флаттера)
    double fade_phase_i;       // Текущая фаза LFO для синфазного канала замираний
    double fade_phase_q;       // Текущая фаза LFO для квадратурного канала замираний
    double fade_inc;           // Шаг фазы LFO на один отсчет
} qshort_channel_sim_t;

/**
 * @brief Расширенная инициализация симулятора КВ-канала
 * @param snr_db Отношение сигнал/шум в дБ (целевое SNR <= -3 дБ)
 * @param freq_offset_hz Начальный сдвиг частоты в Гц
 * @param freq_drift_hz_per_sec Скорость термического дрейфа TCXO (в Гц в секунду)
 * @param fade_freq_hz Частота полярного флаттера (0.0 — замирания выключены)
 */
void channel_sim_init(qshort_channel_sim_t *sim,
                      double snr_db,
                      double freq_offset_hz,
                      double freq_drift_hz_per_sec,
                      double fade_freq_hz,
                      double sample_rate);

// Пропуск одного комплексного отсчета через канал связи (вызывается в цикле 8000 раз в сек)
void channel_sim_process(qshort_channel_sim_t *sim, const cplx_f32 *in_sample, cplx_f32 *out_sample);

#endif // CHANNEL_SIM_H
