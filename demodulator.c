#include "demodulator.h"
#include "complex_filter.h"

// Буфер жесткой задержки на 1 символ (800 сэмплов) для непрерывного каскада
static complex_f continuous_history[NUM_TONES][SYMBOL_LEN] = {0};
static int cont_idx = 0;

// Буфер слепков макушек холмов для дискретного каскада стробов
static complex_f last_strobe_snapshot[NUM_TONES] = {0};
static int is_first_strobe = 1;


void dsp_demodulator_init(demodulator_t *dem, float sample_rate, uint32_t symbol_len) {
    float frequencies[NUM_TONES] = {1300.0f, 1400.0f, 1500.0f, 1600.0f};
    dem->current_window_len = (symbol_len > MAX_SYMBOL_LEN) ? MAX_SYMBOL_LEN : symbol_len;
    dem->history_idx = 0;
    dem->delay_idx = 0;

    for (int i = 0; i < NUM_TONES; i++) {
        dsp_dds_init(&dem->rx_local_dds[i], frequencies[i], sample_rate);
        dem->accumulators[i].re = 0.0f;
        dem->accumulators[i].im = 0.0f;
        dem->last_strobe_snapshot[i].im = dem->last_strobe_snapshot[i].re = 0.0f;

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

void dsp_demodulator_freeze_drift(demodulator_t *dem, const complex_f *current_corr, complex_f *output_frz) {
    // 1. Извлекаем комплексные векторы пилот-тона (1300 Гц)
    complex_f pilot_now  = current_corr[0];
    complex_f pilot_past = dem->corr_history[0][dem->history_idx];

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
        complex_f info_past = dem->corr_history[t][dem->history_idx];

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

        output_frz[t].re = rotated.re * current_amp;
        output_frz[t].im = rotated.im * current_amp;

        // Сохраняем текущее значение в историю для следующего шага конвейера
        dem->corr_history[t][dem->history_idx] = current_corr[t];
    }

    // Инкремент кольцевого индекса истории
    dem->history_idx++;
    if (dem->history_idx >= SYMBOL_LEN) {
    	dem->history_idx = 0;
    }
}

/**
 * КАСКАД 3.a: Вызывается НА КАЖДОМ СЭМПЛЕ.
 * Уничтожает дрейф частоты радиостанций, выпрямляет траектории в бумеранги.
 */
void dsp_demodulator_continuous_freeze(const complex_f *current_corr, complex_f *output_frozen) {
    // 1. Берем текущий пилот и пилот ровно 800 сэмплов назад
    complex_f pilot_now  = current_corr[0];
    complex_f pilot_past = continuous_history[0][cont_idx];

    // Дифференциальный шаг пилота во времени
    complex_f r_pilot;
    r_pilot.re = pilot_now.re * pilot_past.re + pilot_now.im * pilot_past.im;
    r_pilot.im = pilot_now.im * pilot_past.re - pilot_now.re * pilot_past.im;

    float p_mag_sq = r_pilot.re * r_pilot.re + r_pilot.im * r_pilot.im;
    float inv_p_mag = (p_mag_sq > 1e-7f) ? 1.0f / sqrtf(p_mag_sq) : 1.0f;

    // Нормированный сопряженный пилот-вектор для откручивания дрейфа
    complex_f p_comp;
    p_comp.re = r_pilot.re * inv_p_mag;
    p_comp.im = -r_pilot.im * inv_p_mag;

    // 2. Обрабатываем все тона
    for (int t = 0; t < NUM_TONES; t++) {
        complex_f info_now  = current_corr[t];
        complex_f info_past = continuous_history[t][cont_idx];

        // Жесткий временной дифференциальный шаг информационного тона
        complex_f r_info;
        r_info.re = info_now.re * info_past.re + info_now.im * info_past.im;
        r_info.im = info_now.im * info_past.re - info_now.re * info_past.im;

        float common_amp = sqrtf(info_now.re * info_now.re + info_now.im * info_now.im);


        if (t == 0) {
            output_frozen[t].re = common_amp;
            output_frozen[t].im = 0.0f;
        } else {
            // Вычитаем дрейф частоты: вращаем r_info на p_comp
            complex_f rotated;
            rotated.re = r_info.re * p_comp.re - r_info.im * p_comp.im;
            rotated.im = r_info.re * p_comp.im + r_info.im * p_comp.re;

            // Возвращаем линейный масштаб для АРУ верхнего уровня
            float info_mag_sq = rotated.re * rotated.re + rotated.im * rotated.im;
            float inv_info_mag = (info_mag_sq > 1e-7f) ? 1.0f / sqrtf(info_mag_sq) : 1.0f;
            float current_amp = sqrtf(info_now.re * info_now.re + info_now.im * info_now.im);

            output_frozen[t].re = rotated.re * inv_info_mag * common_amp;
            output_frozen[t].im = rotated.im * inv_info_mag * common_amp;
        }

        // Обновляем буфер жесткой временной истории
        continuous_history[t][cont_idx] = current_corr[t];
    }

    cont_idx = (cont_idx + 1) % SYMBOL_LEN;
}

/**
 * КАСКАД 3Б: Вызывается СТРОГО ПО СТРОБУ синхронизатора.
 * Защищает от джиттера, фиксирует созвездие в конечные точки для декодера.
 */
void dsp_demodulator_strobe_latch(const complex_f *frozen_corr, complex_f *output_diff) {
    if (is_first_strobe) {
        for (int t = 0; t < NUM_TONES; t++) {
            last_strobe_snapshot[t] = frozen_corr[t];
            output_diff[t].re = -120.0f; // а было почему-то -12000.0f
            output_diff[t].im = 0.0f;
        }
        is_first_strobe = 0;
        return;
    }
#if 0
    output_diff[0].re = 1.0f;
    output_diff[0].im = 0.0f;

    for (int t = 1; t < NUM_TONES; t++) {
        complex_f now  = frozen_corr[t];
        complex_f past = last_strobe_snapshot[t];

        // Измеряем разность фаз между текущей макушкой холма и предыдущей макушкой холма
        complex_f r_strobe;
        r_strobe.re = now.re * past.re + now.im * past.im;
        r_strobe.im = now.im * past.re - now.re * past.im;

        // Нормируем и возвращаем амплитуду
        float mag_sq = r_strobe.re * r_strobe.re + r_strobe.im * r_strobe.im;
        float inv_mag = (mag_sq > 1e-7f) ? 1.0f / sqrtf(mag_sq) : 1.0f;
        float current_amp = sqrtf(now.re * now.re + now.im * now.im);

        output_diff[t].re = r_strobe.re * inv_mag * current_amp;
        output_diff[t].im = r_strobe.im * inv_mag * current_amp;

        last_strobe_snapshot[t] = now;
    }
#else
    for (int t = 0; t < NUM_TONES; t++) {
        output_diff[t] = frozen_corr[t];
    }
#endif
}

void dsp_demodulator_reset_all_history(void) {
    is_first_strobe = 1;
    cont_idx = 0;
    // Очистить массивы нулями...
}

