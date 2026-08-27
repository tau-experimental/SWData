#include "complex_filter.h"
#include <math.h>

void dsp_complex_bank_init(complex_filter_bank_t *bank, float sample_rate, float bandwidth_hz) {
    float tones[NUM_TONES] = {1300.0f, 1400.0f, 1500.0f, 1600.0f};

    // Вычисляем альфа исходя из желаемой полосы пропускания (например, 40 Гц)
    // Чем меньше полоса, тем меньше alpha, тем дольше раскачка (интегрирование)
    float dt = 1.0f / sample_rate;
    float alpha = 2.0f * M_PI_F * bandwidth_hz * dt;
    if (alpha > 1.0f) alpha = 1.0f;

    for (int i = 0; i < NUM_TONES; i++) {
        float omega = 2.0f * M_PI_F * tones[i] / sample_rate;

        bank->filters[i].alpha = alpha;
        // Нам нужно вращать прошлый сэмпл на угол omega и затухать на (1 - alpha)
        bank->filters[i].feedback_coeff.re = cosf(omega) * (1.0f - alpha);
        bank->filters[i].feedback_coeff.im = sinf(omega) * (1.0f - alpha);

        bank->filters[i].state.re = 0.0f;
        bank->filters[i].state.im = 0.0f;
    }
}

void dsp_complex_bank_process(complex_filter_bank_t *bank, complex_f input, complex_f *outputs) {
    for (int i = 0; i < NUM_TONES; i++) {
        complex_biquad_t *f = &bank->filters[i];

        // Вращение и затухание старого состояния: f->state * f->feedback_coeff
        complex_f rotated_state = c_mul(f->state, f->feedback_coeff);

        // Добавление нового входа: input * alpha
        complex_f scaled_input = { input.re * f->alpha, input.im * f->alpha };

        // Новое состояние
        f->state = c_add(scaled_input, rotated_state);

        // Выход — это и есть отфильтрованный комплексный сигнал данного тона!
        outputs[i] = f->state;
    }
}
