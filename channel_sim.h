#ifndef CHANNEL_SIM_H
#define CHANNEL_SIM_H

#include "wav_io.h" // Нужен для типа cplx_f32

typedef struct {
    double noise_sigma;     // Среднеквадратичное отклонение шума (задается через SNR)
    double freq_offset;     // Сдвиг частоты в Герцах (например, +50 Гц или -20 Гц)
    double sample_rate;     // Частота дискретизации (8000 Гц)
    unsigned int phase_acc; // 32-битный аккумулятор фазы дрейфа частоты
} qshort_channel_sim_t;

// Инициализация симулятора канала
// snr_db — отношение сигнал/шум в децибелах (например, 10 дБ — сильный шум, 30 дБ — чистый эфир)
// freq_offset_hz — дрейф частоты в Гц
void channel_sim_init(qshort_channel_sim_t *sim, double snr_db, double freq_offset_hz, double sample_rate);

// Пропуск одного комплексного отсчета через канал связи
void channel_sim_process(qshort_channel_sim_t *sim, const cplx_f32 *in_sample, cplx_f32 *out_sample);

#endif // CHANNEL_SIM_H
