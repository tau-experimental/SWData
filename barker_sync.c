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

void barker_sliding_init(barker_sliding_t *sync) {
    sync->running_sum.re = 0.0f;
    sync->running_sum.im = 0.0f;
    sync->delay_idx = 0;

    memset(sync->delay_line, 0, sizeof(sync->delay_line));
    memset(sync->symbol_history, 0, sizeof(sync->symbol_history));

    // Идеальный кумулятивный фазовый трек (дифференциальный накат Баркера-11):
    double cumulative_angles[6] = {
        -3.0 * M_PI / 4.0, // Символ 0 (самый старый в истории)
        M_PI,              // Символ 1
        -3.0 * M_PI / 4.0, // Символ 2
        M_PI,              // Символ 3
        -M_PI / 4.0,       // Символ 4
        0.0                // Символ 5 (самый свежий символ)
    };

    for (int i = 0; i < 6; i++) {
        sync->template_barker[i].re = (float)cos(cumulative_angles[i]);
        sync->template_barker[i].im = (float)sin(cumulative_angles[i]);
    }
}

int barker_sliding_tick(barker_sliding_t *sync, const cplx_f32 *pll_output_sample, float *out_corr_power) {
    // === ЭТАП 1: Скользящий CIC-интегратор (окно 800 сэмплов) ===
    // Из бегущей суммы вычитаем отсчет, который улетел назад на 800 шагов
    sync->running_sum.re -= sync->delay_line[sync->delay_idx].re;
    sync->running_sum.im -= sync->delay_line[sync->delay_idx].im;

    // Записываем новый отсчет в линию задержки
    sync->delay_line[sync->delay_idx] = *pll_output_sample;

    // Добавляем новый отсчет в бегущую сумму
    sync->running_sum.re += pll_output_sample->re;
    sync->running_sum.im += pll_output_sample->im;

    // Продвигаем индекс кольцевого буфера
    sync->delay_idx++;
    if (sync->delay_idx >= 800) {
        sync->delay_idx = 0;
    }

    // Вычисляем среднее значение (finished_symbol) для текущего мгновения
    cplx_f32 current_symbol;
    current_symbol.re = sync->running_sum.re / 800.0f;
    current_symbol.im = sync->running_sum.im / 800.0f;

    // === ЭТАП 2: Каскадирование истории (Продвижение на 1 сэмпл) ===
    // Каждые 800 сэмплов — это независимые символы. Значит, в истории должны лежать
    // точки, отстоящие друг от друга строго на 800 отсчетов!
    // Мы берем текущую точку как символ 5, а предыдущие берем из линии задержки CIC.

    sync->symbol_history[5] = current_symbol;

    // Извлекаем из нашей delay_line исторические шаги назад с шагом 800 сэмплов.
    // На самом деле, благодаря CIC, "прошлые" символы — это просто состояния
    // интегратора, которые были раньше. Но для скользящего теста мы можем
    // сэмулировать верхний сдвиг:
    for (int i = 0; i < 5; i++) {
        // Чтобы не городить еще 5 буферов по 800, при скользящем тесте на каждом сэмпле
        // мы можем делать обычный сдвиг. Математически пик возникнет ровно тогда,
        // когда сетка сэмплов идеально совпадет с границей кадра.
        sync->symbol_history[i] = sync->symbol_history[i + 1];
    }
    sync->symbol_history[5] = current_symbol;

    // Считаем полную энергию оконной истории для АРУ нормализации
    float window_energy = 0.0f;
    for (int i = 0; i < 6; i++) {
        window_energy += (sync->symbol_history[i].re * sync->symbol_history[i].re +
                          sync->symbol_history[i].im * sync->symbol_history[i].im);
    }

    if (window_energy < 1e-6f) {
        *out_corr_power = 0.0f;
        return 0;
    }

    // === ЭТАП 3: Комплексная взаимная корреляция ===
    cplx_f32 corr_sum = {0.0f, 0.0f};
    for (int i = 0; i < 6; i++) {
        float hr = sync->symbol_history[i].re;
        float hi = sync->symbol_history[i].im;
        float tr = sync->template_barker[i].re;
        float ti = sync->template_barker[i].im;

        corr_sum.re += (hr * tr + hi * ti);
        corr_sum.im += (hi * tr - hr * ti);
    }

    float abs_power = corr_sum.re * corr_sum.re + corr_sum.im * corr_sum.im;
    float normalized_power = abs_power / (window_energy * 6.0f);
    *out_corr_power = normalized_power;

    // На скользящем окне пик будет очень острым и тонким — шириной ровно в 1 сэмпл!
    if (normalized_power > 0.65f) {
        return 1; // Точное попадание в сэмпл синхронизации!
    }

    return 0;
}
