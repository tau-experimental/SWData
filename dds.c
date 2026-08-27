#include "dds.h"
#include "complex_filter.h"
#include <math.h>

float dds_sin_table[DDS_TABLE_SIZE];

void dsp_dds_init_table(void) {
    for (int i = 0; i < DDS_TABLE_SIZE; i++) {
        dds_sin_table[i] = sinf((2.0f * M_PI_F * i) / DDS_TABLE_SIZE);
    }
}

void dsp_dds_init(dds_t *dds, float frequency, float sample_rate) {
    dds->phase_accumulator = 0;
    dds->phase_step = (uint32_t)((frequency / sample_rate) * 4294967296.0f);
}

void dsp_dds_next_complex(dds_t *dds, float phase_offset_rad, complex_f *out) {
    /* Переводим фазовый сдвиг (например, манипуляцию в радианах) в 32-битное число */
    uint32_t offset = (uint32_t)((phase_offset_rad / (2.0f * M_PI_F)) * 4294967296.0f);
    uint32_t total_phase = dds->phase_accumulator + offset;

    /* Индексы в таблице для синуса и косинуса */
    uint8_t idx_sin = (total_phase >> 24) & DDS_TABLE_MASK;

    /* Косинус — это синус со сдвигом на 90 градусов (1/4 от размера таблицы в 256 значений) */
    uint8_t idx_cos = (idx_sin + (DDS_TABLE_SIZE / 4)) & DDS_TABLE_MASK;

    /* Записываем результат: I = Cos, Q = Sin */
    out->re = dds_sin_table[idx_cos];
    out->im = dds_sin_table[idx_sin];

    /* Шагаем аккумулятором ОДИН раз для следующего комплексного сэмпла */
    dds->phase_accumulator += dds->phase_step;
}

void dsp_dds_set_frequency(dds_t *dds, float frequency, float sample_rate) {
    dds->phase_step = (uint32_t)((frequency / sample_rate) * 4294967296.0f);
}
