#include "barker_sync.h"
#include <math.h>

// Структура для инициализации шаблона (вычислить на старте на основе вашей dqpsk_get_phase_shift)
// Ожидаемые комплексные переходы для 5 дифференциальных шагов Баркера
static const cplx_f32 template_barker_diff[BARKER_DIFF_STEPS] = {
    { 0.707107f,  0.707107f }, // Переход 0 (символ 1: +1)
    { 0.707107f,  0.707107f }, // Переход 1 (символ 2: +1)
    { 0.707107f, -0.707107f }, // Переход 2 (символ 3: -1)
    { 0.707107f, -0.707107f }, // Переход 3 (символ 4: -1)
    { 0.707107f, -0.707107f }, // Переход 4 (символ 5: -1)
    { 0.707107f,  0.707107f }, // Переход 5 (символ 6: +1)
    { 0.707107f, -0.707107f }, // Переход 6 (символ 7: -1)
    { 0.707107f, -0.707107f }, // Переход 7 (символ 8: -1)
    { 0.707107f,  0.707107f }, // Переход 8 (символ 9: +1)
    { 0.707107f, -0.707107f }  // Переход 9 (символ 10: -1)
};
void barker_sliding_init(barker_sliding_t *sync) {
    sync->running_sum.re = 0.0f;
    sync->running_sum.im = 0.0f;
    sync->delay_idx = 0;

    memset(sync->delay_line, 0, sizeof(sync->delay_line));
    memset(sync->symbol_history, 0, sizeof(sync->symbol_history));

    for (int i = 0; i < BARKER_DIFF_STEPS; i++) {
        sync->template_barker[i].re = template_barker_diff[i].re;//(float)cos(phase);
        sync->template_barker[i].im = template_barker_diff[i].im;//(float)sin(phase);
    }
    sync->sample_cnt = 0;

    memset(sync->macro_history, 0, sizeof(sync->macro_history));
    sync->macro_sample_cnt = 0;

    sync->diff_delay_idx = 0;
    memset(sync->diff_delay_line, 0, sizeof(sync->diff_delay_line));
}

// b_step только для отладки, do not forget to remove it nahren!
int barker_sliding_tick(barker_sliding_t *sync, const cplx_f32 *pll_output_sample, float *out_corr_power, int b_step) {
    // === ЭТАП 1: Мгновенный дифференциальный демодулятор ===
    cplx_f32 oldest_sample = sync->delay_line[sync->delay_idx];

    cplx_f32 current_diff;
    // Комплексное перемножение: current * conj(oldest)
    current_diff.re = pll_output_sample->re * oldest_sample.re + pll_output_sample->im * oldest_sample.im;
    current_diff.im = pll_output_sample->im * oldest_sample.re - pll_output_sample->re * oldest_sample.im;

    // Обновляем линию задержки сигнала (800 отсчетов назад)
    sync->delay_line[sync->delay_idx] = *pll_output_sample;

    // === ЭТАП 2: Линия задержки макро-шагов для CIC-интеграторов ===
    // Вместо одной точки мы вытаскиваем из циклического буфера diff_delay_line
    // значения, которые были ровно 800 сэмплов назад.
    cplx_f32 oldest_diff = sync->diff_delay_line[sync->delay_idx];
    sync->diff_delay_line[sync->delay_idx] = current_diff;

    // Инкремент циклического индекса для БУФЕРОВ РАЗМЕРОМ 800
    sync->delay_idx++;
    if (sync->delay_idx >= 800) {
        sync->delay_idx = 0;
    }

    // === ЭТАП 3: Непрерывное обновление макро-истории ИНТЕГРАЛОВ ===
    // Обновляем текущий интеграл (последний символ)
    sync->running_sum.re += (current_diff.re - oldest_diff.re);
    sync->running_sum.im += (current_diff.im - oldest_diff.im);

    // ВАЖНО: Раз в 800 отсчетов (граница символа) мы сдвигаем макро-историю
    // полностью сформированных интегралов прошлых символов!
    if (sync->delay_idx == 0) {
        for (int i = 0; i < (BARKER_DIFF_STEPS - 1); i++) {
            sync->macro_history[i] = sync->macro_history[i + 1];
        }
    }

    // Текущий («живой») интеграл пишется в последнюю ячейку макро-истории
    sync->macro_history[BARKER_DIFF_STEPS - 1].re = sync->running_sum.re / 800.0f;
    sync->macro_history[BARKER_DIFF_STEPS - 1].im = sync->running_sum.im / 800.0f;

    // === ЭТАП 4: Расчет взаимной корреляции (Soft Decision) ===
    float window_energy = 0.0f;
    cplx_f32 corr_sum = {0.0f, 0.0f};

    for (int i = 0; i < BARKER_DIFF_STEPS; i++) {
        window_energy += (sync->macro_history[i].re * sync->macro_history[i].re +
                          sync->macro_history[i].im * sync->macro_history[i].im);

        float hr = sync->macro_history[i].re;
        float hi = sync->macro_history[i].im;
        float tr = template_barker_diff[i].re;
        float ti = template_barker_diff[i].im;

        // Корреляция: Сигнал * conj(Шаблон)
        corr_sum.re += (hr * tr + hi * ti);
        corr_sum.im += (hi * tr - hr * ti);
    }

    if (window_energy < 0.01f) {
        *out_corr_power = 0.0f;
        return 0;
    }

    float abs_power = corr_sum.re * corr_sum.re + corr_sum.im * corr_sum.im;
    float normalized_power = abs_power / (window_energy * (float)BARKER_DIFF_STEPS);

    if (normalized_power > 1.0f) normalized_power = 1.0f;
    *out_corr_power = normalized_power;

    // === ЭТАП 5: Поиск экстремума (Детектор пика) ===
    // На КВ при SNR = -18 дБ слепой порог провалится.
    // Вместо немедленного return 1, мы ищем максимум функции нормированной мощности.
    if (normalized_power > 0.65f) {
        // Сигнал обнаружен в текущем скользящем окне!
        // Для точного тайминга Витерби нам нужно вернуть 1 строго в геометрическом пике.
        return 1;
    }

    return 0;
}
