#ifndef DEMODULATOR_H
#define DEMODULATOR_H

#include <stdint.h>
#include "dsp_utils.h"
#include "dds.h"
#include "complex_filter.h"


#define MAX_SYMBOL_LEN 1000

typedef struct {
    dds_t rx_local_dds[NUM_TONES];

    // 1. История для скользящего интегратора (деротированные сэмплы)
    complex_f history[NUM_TONES][MAX_SYMBOL_LEN];
    uint32_t history_idx;
    complex_f accumulators[NUM_TONES];
    uint32_t current_window_len;

    // 2. Новое: Буфер задержки комплексных выходов коррелятора на 1 символ
    complex_f output_delay_buf[NUM_TONES][MAX_SYMBOL_LEN];
    uint32_t delay_idx;
} demodulator_t;

void dsp_demodulator_init(demodulator_t *dem, float sample_rate, uint32_t symbol_len);
void dsp_demodulator_step(demodulator_t *dem, const complex_f *rx_filtered_inputs, complex_f *outputs);

/* Новая функция: вычисляет дифференциальный комплексный вектор для каждого тона */
void dsp_demodulator_get_diff(demodulator_t *dem, const complex_f *current_outputs, complex_f *diff_outputs);

#endif /* DEMODULATOR_H */
