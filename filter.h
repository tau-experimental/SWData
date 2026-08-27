#ifndef FILTER_H
#define FILTER_H

#define NUM_TONES 4

/* Коэффициенты Biquad фильтра */
typedef struct {
    float b0, b1, b2;
    float a1, a2;
} biquad_coeffs_t;

/* Состояние (история) фильтра */
typedef struct {
    float w1;
    float w2;
} biquad_state_t;

/* Контейнер для блока из 4-х полосовых фильтров */
typedef struct {
    biquad_coeffs_t coeffs[NUM_TONES];
    biquad_state_t state[NUM_TONES];
} filter_bank_t;

void dsp_filter_init_bandpass(biquad_coeffs_t *coeffs, float center_freq, float q_factor, float sample_rate);
void dsp_filter_bank_init(filter_bank_t *bank, float sample_rate);
void dsp_filter_bank_process(filter_bank_t *bank, float input, float *outputs);

#endif /* FILTER_H */
