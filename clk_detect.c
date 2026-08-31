#include "clk_detect.h"
#if 0
void clk_detect_init(clk_detect_t *clk) {
    clk->ptr = 0;
    clk->running_sum.re = 0.0f;
    clk->running_sum.im = 0.0f;
    memset(clk->delay_line, 0, sizeof(clk->delay_line));
    memset(clk->diff_buffer, 0, sizeof(clk->diff_buffer));
}

// Возвращает мгновенную когерентную мощность полусимвольного окна
float clk_detect_tick(clk_detect_t *clk, const cplx_f32 *input_sample) {
    // 1. Дифференциальный демодулятор на интервале ПОЛУСИМВОЛА
    cplx_f32 oldest_sample = clk->delay_line[clk->ptr];
    cplx_f32 curr_diff;
    curr_diff.re = input_sample->re * oldest_sample.re + input_sample->im * oldest_sample.im;
    curr_diff.im = input_sample->im * oldest_sample.re - input_sample->re * oldest_sample.im;
    clk->delay_line[clk->ptr] = *input_sample;

    // 2. Скользящее CIC-окно на 400 элементов
    cplx_f32 oldest_diff = clk->diff_buffer[clk->ptr];
    clk->diff_buffer[clk->ptr] = curr_diff;

    // Инкремент циклического индекса
    clk->ptr++;
    if (clk->ptr >= HALF_SYM_LEN) {
        clk->ptr = 0;
    }

    // Обновляем когерентную сумму
    clk->running_sum.re += (curr_diff.re - oldest_diff.re);
    clk->running_sum.im += (curr_diff.im - oldest_diff.im);

    // 3. Вычисляем чистую ненормированную мощность интеграла
    float power = (clk->running_sum.re * clk->running_sum.re +
                   clk->running_sum.im * clk->running_sum.im);

    return power;
}
#endif
