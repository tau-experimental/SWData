#include "demodulator.h"
#include "complex_filter.h"

// Кольцевые буферы выходов корреляторов для задержки на 1 символ (800 сэмплов)
static complex_f corr_history[NUM_TONES][SYMBOL_LEN];
static int history_idx = 0;

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

void process_continuous_differential_tracking(const complex_f *current_corr, complex_f *output_diff) {
    // 1. Извлекаем комплексные векторы пилот-тона (1300 Гц)
    complex_f pilot_now  = current_corr[0];
    complex_f pilot_past = corr_history[0][history_idx];

    // 2. Вычисляем сырой дифференциальный вектор пилот-тона: R_pilot = pilot_now * conj(pilot_past)
    complex_f r_pilot;
    r_pilot.re = pilot_now.re * pilot_past.re + pilot_now.im * pilot_past.im;
    r_pilot.im = pilot_now.im * pilot_past.re - pilot_now.re * pilot_past.im;

    // КРИТИЧЕСКИЙ МОМЕНТ: Нормируем вектор пилота по амплитуде, чтобы он не искажал
    // энергетический масштаб информационных каналов, но сохранял чистую фазу.
    float pilot_mag_sq = r_pilot.re * r_pilot.re + r_pilot.im * r_pilot.im;

    // Защита от деления на 0 при выключении передатчика (шумовая пыль)
    float inv_pilot_mag = 1.0f;
    if (pilot_mag_sq > 1e-7f) {
        inv_pilot_mag = 1.0f / sqrtf(pilot_mag_sq);
    }

    // Нормированный сопряженный вектор пилота для «откручивания» частоты назад
    complex_f pilot_compensation;
    pilot_compensation.re = r_pilot.re * inv_pilot_mag;
    pilot_compensation.im = -r_pilot.im * inv_pilot_mag; // Минус обеспечивает сопряжение (conj)

    // 3. Компенсируем и докручиваем все каналы гребенки
    for (int t = 0; t < NUM_TONES; t++) {
        complex_f info_now  = current_corr[t];
        complex_f info_past = corr_history[t][history_idx];

        // Сырой дифференциальный шаг информационного тона
        complex_f r_info;
        r_info.re = info_now.re * info_past.re + info_now.im * info_past.im;
        r_info.im = info_now.im * info_past.re - info_now.re * info_past.im;

        float info_mag_sq = r_info.re * r_info.re + r_info.im * r_info.im;
        float inv_info_mag = 1.0f;
        if (info_mag_sq > 1e-7f) {
            inv_info_mag = 1.0f / sqrtf(info_mag_sq);
        }

        // Извлекаем чистую, нормированную фазу информационного тона (длина вектора = 1.0)
        complex_f r_info_norm;
        r_info_norm.re = r_info.re * inv_info_mag;
        r_info_norm.im = r_info.im * inv_info_mag;

        complex_f rotated;
        rotated.re = r_info_norm.re * pilot_compensation.re - r_info_norm.im * pilot_compensation.im;
        rotated.im = r_info_norm.re * pilot_compensation.im + r_info_norm.im * pilot_compensation.re;

        // Теперь возвращаем вектору ЛИНЕЙНУЮ текущую амплитуду текущего сэмпла.
        // Это восстановит холмы энергии для Каскада 4 и Каскада 6 (АРУ), но уберет квадратичный разнос.
        float current_amp = sqrtf(info_now.re * info_now.re + info_now.im * info_now.im);

        output_diff[t].re = rotated.re * current_amp;
        output_diff[t].im = rotated.im * current_amp;

        // Сохраняем текущее значение в историю для следующего шага конвейера
        corr_history[t][history_idx] = current_corr[t];
    }

    // Инкремент кольцевого индекса истории
    history_idx++;
    if (history_idx >= SYMBOL_LEN) {
        history_idx = 0;
    }
}

