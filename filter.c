#include "filter.h"
#include "dsp_utils.h"
#include <math.h>

/* Расчет коэффициентов полосового БИХ-фильтра постоянного добротности (Q) */
void dsp_filter_init_bandpass(biquad_coeffs_t *coeffs, float center_freq, float q_factor, float sample_rate) {
    float omega = 2.0f * M_PI_F * center_freq / sample_rate;
    float sin_w = sinf(omega);
    float cos_w = cosf(omega);
    float alpha = sin_w / (2.0f * q_factor);

    float a0 = 1.0f + alpha;
    coeffs->b0 = alpha / a0;
    coeffs->b1 = 0.0f;
    coeffs->b2 = -alpha / a0;
    coeffs->a1 = (-2.0f * cos_w) / a0;
    coeffs->a2 = (1.0f - alpha) / a0;
}

void dsp_filter_bank_init(filter_bank_t *bank, float sample_rate) {
    float tones[NUM_TONES] = {1300.0f, 1400.0f, 1500.0f, 1600.0f};
    /* Добротность Q=15..20 даст достаточно узкую полосу, чтобы разделить тона */
    float q_factor = 20.0f;

    for (int i = 0; i < NUM_TONES; i++) {
        dsp_filter_init_bandpass(&bank->coeffs[i], tones[i], q_factor, sample_rate);
        bank->state[i].w1 = 0.0f;
        bank->state[i].w2 = 0.0f;
    }
}

/* Прямая форма II (Direct Form II) — экономит память */
static inline float biquad_process(const biquad_coeffs_t *c, biquad_state_t *s, float in) {
    float w0 = in - c->a1 * s->w1 - c->a2 * s->w2;
    float out = c->b0 * w0 + c->b1 * s->w1 + c->b2 * s->w2;
    s->w2 = s->w1;
    s->w1 = w0;
    return out;
}

void dsp_filter_bank_process(filter_bank_t *bank, float input, float *outputs) {
    for (int i = 0; i < NUM_TONES; i++) {
        outputs[i] = biquad_process(&bank->coeffs[i], &bank->state[i], input);
    }
}
