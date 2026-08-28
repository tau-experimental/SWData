#ifndef DEMODULATOR_H
#define DEMODULATOR_H

#include <stdint.h>
#include "dsp_utils.h"
#include "dds.h"
#include "complex_filter.h"

#define FS 8000.0f
#define SYMBOL_LEN 800
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
    complex_f corr_history[NUM_TONES][SYMBOL_LEN];
    complex_f last_strobe_snapshot[NUM_TONES];
    int is_first_strobe;
} demodulator_t;

void dsp_demodulator_init(demodulator_t *dem, float sample_rate, uint32_t symbol_len);
void dsp_demodulator_step(demodulator_t *dem, const complex_f *rx_filtered_inputs, complex_f *outputs);

/* Новая функция: вычисляет дифференциальный комплексный вектор для каждого тона */
void dsp_demodulator_get_diff(demodulator_t *dem, const complex_f *current_outputs, complex_f *diff_outputs);
// непрерывный докручиватель
void dsp_demodulator_freeze_drift(demodulator_t *dem, const complex_f *current_corr, complex_f *output_diff);
void dsp_demodulator_process_strobe_with_pilot(const complex_f *current_corr, complex_f *output_diff);
void dsp_demodulator_reset_all_history(void);
void dsp_demodulator_strobe_latch(const complex_f *frozen_corr, complex_f *output_diff);
void dsp_demodulator_continuous_freeze(const complex_f *current_corr, complex_f *output_frozen);


#endif /* DEMODULATOR_H */
