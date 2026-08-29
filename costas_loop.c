#include "costas_loop.h"
#include <math.h>

#define SGN(x) ((x > 0.0) - (x < 0.0))

void costas_loop_init(costas_loop_t *costas, short *sine_table_ptr) {
    costas->phase_acc = 0;
    costas->sine_lut = sine_table_ptr;

    // Базовая частота удержания — строго наши 1000 Гц ПЧ (FTW = 8192)
    costas->phase_step_inc = 0; //8192;

    // Коэффициенты удержания для петли Костаса
    // Они должны быть очень маленькими, чтобы петля не реагировала на шумы
    costas->kp = 0.1f;//0.08f; //0.015f;
    costas->ki = 0.01f;//0.002f;// 0.0004f;
    costas->freq_integrator = 0.0f;
}

void costas_loop_tick(costas_loop_t *costas, const cplx_f32 *in_sample, cplx_f32 *out_sample) {
    // 1. Поворот локальным DDS петли
    unsigned char sin_idx = (unsigned char)(costas->phase_acc >> 8);
    unsigned char cos_idx = (unsigned char)((sin_idx + 64) & 0xFF);

    float local_cos = (float)costas->sine_lut[cos_idx] / 32000.0f;
    float local_sin = (float)costas->sine_lut[sin_idx] / 32000.0f;

    out_sample->re = in_sample->re * local_cos + in_sample->im * local_sin; // был +
    out_sample->im = in_sample->im * local_cos - in_sample->re * local_sin; // был -

    // 2. QPSK дискриминатор
    float sign_re = (out_sample->re > 0.0f) ? 1.0f : -1.0f;
    float sign_im = (out_sample->im > 0.0f) ? 1.0f : -1.0f;
    //float raw_phase_error = out_sample->im * sign_re - out_sample->re * sign_im;
    // Альтернативный, ультра-жесткий вариант (Sign-Sign):
    float raw_phase_error = SGN(out_sample->im) * sign_re - SGN(out_sample->re) * sign_im;

    // ЗАЩИТА ДЛЯ SNR = -12 дБ: Скользящий экспоненциальный фильтр фазовой ошибки
    // Он гасит быстрые хаотичные удары шума, пропуская только медленный тренд расстройки
    static float filtered_error = 0.0f;
    //filtered_error = filtered_error + 0.02f * (raw_phase_error - filtered_error);
    filtered_error = filtered_error + 0.2f * (raw_phase_error - filtered_error);

    if (filtered_error > 1.0f)  filtered_error = 1.0f;
    if (filtered_error < -1.0f) filtered_error = -1.0f;

    // 3. Интегратор частоты
    costas->freq_integrator += costas->ki * filtered_error;
    if (costas->freq_integrator > 1.28f)  costas->freq_integrator = 1.28f;
    if (costas->freq_integrator < -1.28f) costas->freq_integrator = -1.28f;

    // 4. Шаг DDS
    float total_step = (float)costas->phase_step_inc +
                       (costas->kp * filtered_error * 256.0f) +
                       (costas->freq_integrator * 256.0f);

    costas->phase_acc = (unsigned short)(costas->phase_acc + (unsigned short)total_step);
}

void costas_loop_gear_shift(costas_loop_t *costas) {
    // ВМЕСТО ОБНУЛЕНИЯ: Переводим интегратор частоты в режим "микроскопа"
    // Петля становится экстремально инертным тяжелым маховиком
    costas->ki = 0.000005f; // Уменьшаем в 400 раз!
    costas->kp = 0.005000f; // Уменьшаем в 16 раз!
}

