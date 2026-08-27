#include "demodulator.h"

void dsp_demodulator_init(demodulator_t *dem, float sample_rate, uint32_t symbol_len) {
    float frequencies[NUM_TONES] = {1300.0f, 1400.0f, 1500.0f, 1600.0f};
    dem->current_window_len = (symbol_len > MAX_SYMBOL_LEN) ? MAX_SYMBOL_LEN : symbol_len;
    dem->history_idx = 0;
    dem->delay_idx = 0;

    for (int i = 0; i < NUM_TONES; i++) {
        dsp_dds_init(&dem->rx_local_dds[i], frequencies[i], sample_rate);
        dem->accumulators[i].re = 0.0f;
        dem->accumulators[i].im = 0.0f;

        for (int j = 0; j < MAX_SYMBOL_LEN; j++) {
            dem->history[i][j].re = 0.0f;
            dem->history[i][j].im = 0.0f;
            dem->output_delay_buf[i][j].re = 0.0f;
            dem->output_delay_buf[i][j].im = 0.0f;
        }
    }
}

void dsp_demodulator_step(demodulator_t *dem, const complex_f *rx_filtered_inputs, complex_f *outputs) {
    uint32_t old_idx = (dem->history_idx + 1) % dem->current_window_len;

    for (int i = 0; i < NUM_TONES; i++) {
        // 1. Генерируем локальный опорный вектор
        complex_f local_vector;
        dsp_dds_next_complex(&dem->rx_local_dds[i], 0.0f, &local_vector);

        // Комплексное сопряжение опорного сигнала (меняем знак мнимой части)
        local_vector.im = -local_vector.im;

        // 2. Умножаем входной сигнал на сопряженный опорный (останавливаем частоту)
        // Вход берем с фильтра соответствующего тона
        complex_f derotated = c_mul(rx_filtered_inputs[i], local_vector);

        // 3. Алгоритм скользящего окна (Moving Average)
        // Вычитаем из накопителя самый старый сэмпл, который сейчас покидает окно
        dem->accumulators[i] = c_sub(dem->accumulators[i], dem->history[i][dem->history_idx]);

        // Записываем новый сэмпл на место старого в историю
        dem->history[i][dem->history_idx] = derotated;

        // Прибавляем новый сэмпл к накопителю
        dem->accumulators[i] = c_add(dem->accumulators[i], derotated);

        // Возвращаем текущее комплексное состояние интегратора для данного тона
        outputs[i] = dem->accumulators[i];
    }

    // Шагаем по кольцевому буферу
    dem->history_idx = (dem->history_idx + 1) % dem->current_window_len;
}

void dsp_demodulator_get_diff(demodulator_t *dem, const complex_f *current_outputs, complex_f *diff_outputs) {
    for (int i = 0; i < NUM_TONES; i++) {
        // Извлекаем вектор, который был ровно SYMBOL_LEN сэмплов назад
        complex_f old_vector = dem->output_delay_buf[i][dem->delay_idx];

        // Комплексное сопряжение старого вектора
        old_vector.im = -old_vector.im;

        // Перемножаем текущий вектор на сопряженный старый
        diff_outputs[i] = c_mul(current_outputs[i], old_vector);

        // Сохраняем текущий вектор в буфер задержки на место прочитанного
        dem->output_delay_buf[i][dem->delay_idx] = current_outputs[i];
    }

    // Шагаем по кольцевому буферу задержки
    dem->delay_idx = (dem->delay_idx + 1) % dem->current_window_len;
}

