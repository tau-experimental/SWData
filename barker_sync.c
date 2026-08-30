#include "barker_sync.h"
#include <math.h>

void barker_sync_init(barker_sync_t *sync) {
    sync->current_integrator.re = 0.0f;
    sync->current_integrator.im = 0.0f;
    sync->sample_counter = 0;

    for (int i = 0; i < 6; i++) {
        sync->symbol_history[i].re = 0.0f;
        sync->symbol_history[i].im = 0.0f;
    }

    // КУМУЛЯТИВНЫЕ фазы для дибитов Баркера-11 (с учетом дифференциального наката):
    // Символ 0 (накоплено -135°) -> re=-0.707, im=-0.707
    // Символ 1 (накоплено -180°) -> re=-1.000, im= 0.000
    // Символ 2 (накоплено -135°) -> re=-0.707, im=-0.707
    // Символ 3 (накоплено -180°) -> re=-1.000, im= 0.000
    // Символ 4 (накоплено  -45°) -> re= 0.707, im=-0.707
    // Символ 5 (накоплено    0°) -> re= 1.000, im= 0.000

    double cumulative_angles[6] = {
        -3.0 * M_PI / 4.0, // Символ 0
        M_PI,              // Символ 1
        -3.0 * M_PI / 4.0, // Символ 2
        M_PI,              // Символ 3
        -M_PI / 4.0,       // Символ 4
        0.0                // Символ 5
    };

    for (int i = 0; i < 6; i++) {
        sync->template_barker[i].re = (float)cos(cumulative_angles[i]);
        sync->template_barker[i].im = (float)sin(cumulative_angles[i]);
    }
}

int barker_sync_tick(barker_sync_t *sync, const cplx_f32 *pll_output_sample, float *out_corr_power) {
    // 1. Интегрируем (копим энергию символа, вычищая КВ-шум)
    sync->current_integrator.re += pll_output_sample->re;
    sync->current_integrator.im += pll_output_sample->im;
    sync->sample_counter++;

    // Ждем окончания символа (800 отсчетов при 10 Бод)
    if (sync->sample_counter < 800) {
        *out_corr_power = 0.0f;
        return 0;
    }

    // --- СИМВОЛ ЗАВЕРШЕН (Integrate and Dump) ---
    cplx_f32 finished_symbol;
    finished_symbol.re = sync->current_integrator.re / 800.0f;
    finished_symbol.im = sync->current_integrator.im / 800.0f;

    // Сбрасываем интегратор
    sync->current_integrator.re = 0.0f;
    sync->current_integrator.im = 0.0f;
    sync->sample_counter = 0;

    // Сдвигаем историю в буфере и параллельно считаем полную энергию текущего окна
    float window_energy = 0.0f;
    for (int i = 0; i < 5; i++) {
        sync->symbol_history[i] = sync->symbol_history[i + 1];
        window_energy += (sync->symbol_history[i].re * sync->symbol_history[i].re +
                          sync->symbol_history[i].im * sync->symbol_history[i].im);
    }
    sync->symbol_history[5] = finished_symbol;
    window_energy += (finished_symbol.re * finished_symbol.re + finished_symbol.im * finished_symbol.im);

    // Защита от деления на ноль при отсутствии сигнала (полная тишина)
    if (window_energy < 1e-6f) {
        *out_corr_power = 0.0f;
        return 0;
    }

    // 2. Взаимная комплексная корреляция
    cplx_f32 corr_sum = {0.0f, 0.0f};
    for (int i = 0; i < 6; i++) {
        float hr = sync->symbol_history[i].re;
        float hi = sync->symbol_history[i].im;
        float tr = sync->template_barker[i].re;
        float ti = sync->template_barker[i].im;

        // Комплексное умножение на сопряженное: (hr + j*hi) * (tr - j*ti)
        corr_sum.re += (hr * tr + hi * ti);
        corr_sum.im += (hi * tr - hr * ti);
    }

    // Абсолютная мощность корреляции
    float abs_power = corr_sum.re * corr_sum.re + corr_sum.im * corr_sum.im;

    // ДИНАМИЧЕСКАЯ НОРМАЛИЗАЦИЯ (AGC):
    // Шаблон template_barker нормирован (сумма квадратов его модулей = 6.0).
    // Нормированная мощность = abs_power / (Энергия_окна * Энергия_шаблона)
    float normalized_power = abs_power / (window_energy * 6.0f);
    *out_corr_power = normalized_power;

    // Относительный порог. 1.0 — идеальное совпадение фаз.
    // Для КВ-канала (SNR = 6 дБ) с новым Баркер-11 порог 0.45 — железобетонный.
    if (normalized_power > 0.45f) {
        return 1; // УСПЕХ: Истинный маркер преамбулы обнаружен!
    }

    return 0;
}

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
        //double phase = (template_barker_diff[i].re > 0) ? 0 : M_PI;
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
#if 0
    // === ЭТАП 1: Дифференциальный демодулятор на лету ===
    // Извлекаем сэмпл, который был ровно 800 отсчетов назад
    cplx_f32 oldest_sample = sync->delay_line[sync->delay_idx];

    // Вычисляем мгновенный дифференциальный переход между "сейчас" и "800 сэмплов назад"
    // Формула: diff = pll_output_sample * conj(oldest_sample)
    cplx_f32 current_diff;
    current_diff.re = pll_output_sample->re * oldest_sample.re + pll_output_sample->im * oldest_sample.im;
    current_diff.im = pll_output_sample->im * oldest_sample.re - pll_output_sample->re * oldest_sample.im;

    // Обновляем линию задержки сырых сэмплов
    sync->delay_line[sync->delay_idx] = *pll_output_sample;
    sync->delay_idx++;
    if (sync->delay_idx >= 800) sync->delay_idx = 0;

    // === ЭТАП 2: Скользящая история 5 дифференциальных шагов ===
    // Чтобы проверить корреляцию 5 переходов, нам нужно знать, какими были
    // дифференциальные переходы 800, 1600, 2400, 3200 сэмплов назад.
    // Для этого нам нужен кольцевой буфер ДИФФЕРЕНЦИАЛЬНЫХ переходов на 4000 элементов
    sync->cic_history[sync->cic_hist_idx] = current_diff;

    cplx_f32 d[6];
    for (int i = 0; i < 5; i++) {
        int steps_back = (4 - i) * 800; // 5 переходов (индексы 0..4)
        int hist_idx = sync->cic_hist_idx - steps_back;
        if (hist_idx < 0) hist_idx += 4000; // Буфер cic_history теперь равен 4000 элементов!

        d[i] = sync->cic_history[hist_idx];
    }

    sync->cic_hist_idx++;
    if (sync->cic_hist_idx >= 4000) sync->cic_hist_idx = 0;

    // Считаем энергию дифференциального окна (теперь модули будут около 1.0!)
    float window_energy = 0.0f;
    for (int i = 0; i < 5; i++) {
        window_energy += (d[i].re * d[i].re + d[i].im * d[i].im);
    }

    // Жесткая отсечка тишины (для амплитуды 1.0 энергия 5 элементов будет около 5.0)
    if (window_energy < 1e-4f) {
        *out_corr_power = 0.0f;
        return 0;
    }

    // === ЭТАП 3: Взаимная корреляция векторов ===
    cplx_f32 corr_sum = {0.0f, 0.0f};
    for (int i = 0; i < 5; i++) {
        // Скалярное произведение с комплексным сопряжением шаблона (d * conj(template))
        corr_sum.re += (d[i].re * template_barker_diff[i].re + d[i].im * template_barker_diff[i].im);
        corr_sum.im += (d[i].im * template_barker_diff[i].re - d[i].re * template_barker_diff[i].im);
    }

    float abs_power = corr_sum.re * corr_sum.re + corr_sum.im * corr_sum.im;
    float normalized_power = abs_power / (window_energy * 5.0f);
    *out_corr_power = normalized_power;

    // Временный диагностический шпион
    if (b_step == 16276) {
        printf("\n🕵️‍♂️ [МГНОВЕННЫЙ ШПИОН]:\n");
        printf("  -> window_energy: %f\n", window_energy);
        printf("  -> normalized_power: %f\n", normalized_power);
    }

    if (normalized_power > 0.75f) return 1;
    return 0;
#else
    // === ЭТАП 1: Мгновенный дифференциальный демодулятор ===
    // === ЭТАП 1: Мгновенный дифференциальный демодулятор ===
    cplx_f32 oldest_sample = sync->delay_line[sync->delay_idx];

    cplx_f32 current_diff;
    current_diff.re = pll_output_sample->re * oldest_sample.re + pll_output_sample->im * oldest_sample.im;
    current_diff.im = pll_output_sample->im * oldest_sample.re - pll_output_sample->re * oldest_sample.im;

    sync->delay_line[sync->delay_idx] = *pll_output_sample;

    // === ЭТАП 2: Скользящий CIC-интегратор над дифференциальным сигналом ===
    cplx_f32 oldest_diff = sync->diff_delay_line[sync->delay_idx];

    sync->running_sum.re -= oldest_diff.re;
    sync->running_sum.im -= oldest_diff.im;

    sync->diff_delay_line[sync->delay_idx] = current_diff;

    sync->running_sum.re += current_diff.re;
    sync->running_sum.im += current_diff.im;

    sync->delay_idx++;
    if (sync->delay_idx >= 800) sync->delay_idx = 0;

    // Свежее интегрированное значение дифференциального перехода
    cplx_f32 integrated_diff;
    integrated_diff.re = sync->running_sum.re / 800.0f;
    integrated_diff.im = sync->running_sum.im / 800.0f;

    // === ЭТАП 3: Синхронное скольжение макро-истории ===
    sync->macro_history[BARKER_DIFF_STEPS - 1] = integrated_diff;

    // Расчет энергии и взаимной корреляции векторов
    float window_energy = 0.0f;
    cplx_f32 corr_sum = {0.0f, 0.0f};

    for (int i = 0; i < BARKER_DIFF_STEPS; i++) {
        window_energy += (sync->macro_history[i].re * sync->macro_history[i].re +
                          sync->macro_history[i].im * sync->macro_history[i].im);

        float hr = sync->macro_history[i].re;
        float hi = sync->macro_history[i].im;
        float tr = template_barker_diff[i].re;
        float ti = template_barker_diff[i].im;

        // Взаимная корреляция: Сигнал * conj(Шаблон)
        corr_sum.re += (hr * tr + hi * ti);
        corr_sum.im += (hi * tr - hr * ti);
    }

    // ЖЕСТКАЯ ЗАЩИТА ОТ ТИШИНЫ И ДЕЛЕНИЯ НА НОЛЬ
    // Для нормального сигнала амплитудой ~1.0 суммарная энергия 10 элементов должна быть около 10.0.
    // Если она падает ниже 0.01 — это либо тишина, либо ортогональная каша. Обнуляем!
    if (window_energy < 0.01f) {
        *out_corr_power = 0.0f;
        return 0;
    }

    float abs_power = corr_sum.re * corr_sum.re + corr_sum.im * corr_sum.im;
    float normalized_power = abs_power / (window_energy * (float)BARKER_DIFF_STEPS);

    // Защитный хак от математического джиттера: ограничиваем потолок единицей
    if (normalized_power > 1.0f) normalized_power = 1.0f;

    *out_corr_power = normalized_power;

    // === ЭТАП 4: Дискретный сдвиг стабильного прошлого ===
    if (sync->delay_idx == 0) {
        for (int i = 0; i < (BARKER_DIFF_STEPS - 1); i++) {
            sync->macro_history[i] = sync->macro_history[i + 1];
        }
    }

    // Для DBPSK-11 порог можно смело ставить на 0.75..0.80
    if (normalized_power > 0.65f) return 1;
    return 0;

#endif
}
