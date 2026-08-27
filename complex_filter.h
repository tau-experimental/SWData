#ifndef COMPLEX_FILTER_H
#define COMPLEX_FILTER_H

#include "dsp_utils.h"

#define NUM_TONES 4

typedef struct {
    complex_f feedback_coeff; // Коэффициент обратной связи (учитывает частоту и затухание)
    float alpha;              // Коэффициент входного сигнала
    complex_f state;          // Текущее состояние (история / комплексный выход)
} complex_biquad_t; // По сути, одномерное комплексное звено

typedef struct {
    complex_biquad_t filters[NUM_TONES];
} complex_filter_bank_t;

void dsp_complex_bank_init(complex_filter_bank_t *bank, float sample_rate, float bandwidth_hz);
void dsp_complex_bank_process(complex_filter_bank_t *bank, complex_f input, complex_f *outputs);

#endif /* COMPLEX_FILTER_H */
