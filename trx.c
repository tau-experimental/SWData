#include "trx.h"
#include "dds.h"
#include <stdlib.h>
#include <time.h>

/*const uint8_t mls_31[MLS31_LENGTH] = {
	    1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 0,
	    0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, 0, 0
	};*/

// Эталонная преамбула, переведенная в полярный формат (+1 / -1) для коррелятора
const int8_t mls_31_polar[MLS31_LENGTH] = {
    1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, -1, -1,
    -1, -1, 1, -1, 1, -1, 1, 1, 1, -1, 1, 1, -1, -1, -1
};

void generate_test_packet(float *buf_i, float *buf_q, int *total_samples) {
    init_dds_table();
    dds_t dds = {0, 0};
    int offset = 0;
    float opening_silence, closing_silence;
    srand((unsigned) time(0));

    opening_silence = 0.35 + 0.2*rand()/(float)RAND_MAX;
    closing_silence = 0.3 + 0.2*rand()/(float)RAND_MAX;

    // 1. Рандомная тишина (возьмем среднее 0.35 сек)
    generate_silence(opening_silence, buf_i, buf_q, &offset);

    // 2. Пилот-тон (пусть будет 1.5 секунды на базовой частоте 400 Гц)
    generate_tone(&dds, TONE(0), (int)(1.5f * FS), buf_i, buf_q, &offset);

    // 3. Преамбула MLS-31 (2-MFSK)
    for (int i = 0; i < 31; i++) {
        float freq = (mls_31_polar[i] == 1) ? TONE(3) : TONE(0);
        generate_tone(&dds, freq, SAMPLES_PER_SYMBOL, buf_i, buf_q, &offset);
    }

    // 4. Финальный пилот (0.5 секунды для отладки)
    generate_tone(&dds, TONE(0), (int)(0.5f * FS), buf_i, buf_q, &offset);

    // 5. Финальная тишина (0.3 секунды)
    generate_silence(closing_silence, buf_i, buf_q, &offset);

    *total_samples = offset;
}
/*-----------------------------------------------------------------------------*/
// Инициализация передачи нового кадра
void tx_machine_start(tx_machine_t *tx, const uint8_t *data, uint16_t data_len) {
    tx->state = STATE_INITIAL_SILENCE;
    tx->dds.phase_accumulator = 0;
    tx->dds.phase_step = 0;
    tx->sample_counter = 0;
    tx->symbol_counter = 0;
    tx->is_active = true;

    tx->raw_data_bytes = data;
	tx->total_bytes = data_len;
	tx->dibit_index = 0; // Итератор скремблера сбросится автоматически, увидев 0
}

// Генерация одного I/Q сэмпла «на лету»
// Возвращает true, если кадр еще передается, и false, если трансляция окончена
bool tx_machine_process_sample(tx_machine_t *tx, float *out_i, float *out_q) {
    if (!tx->is_active || tx->state == STATE_IDLE) {
        *out_i = 0.0f;
        *out_q = 0.0f;
        tx->state = STATE_IDLE;
        tx->is_active = false;
        return false;
    }

    switch (tx->state) {
        case STATE_INITIAL_SILENCE:
            *out_i = 0.0f;
            *out_q = 0.0f;
            tx->sample_counter++;

            // 0.35 секунды тишины = 0.35 * 8000 = 2800 сэмплов
            //if (tx->sample_counter >= (uint32_t)(0.35f * FS)) {
            if (tx->sample_counter >= 5) {
                tx->state = STATE_LEAD_IN_PILOT;
                tx->sample_counter = 0;
                // Задаем тон А (400 Гц) для пилота
                dds_set_tone(&tx->dds, TONE_A);
            }
            break;

        case STATE_LEAD_IN_PILOT:
            dds_generate_sample(&tx->dds, out_i, out_q);
            tx->sample_counter++;

            // 1.5 секунды пилота = 12000 сэмплов
            if (tx->sample_counter >= (uint32_t)(1.5f * FS)) {
                tx->state = STATE_PREAMBLE;
                tx->sample_counter = 0;
                tx->symbol_counter = 0;

                // Устанавливаем частоту для самого первого символа преамбулы
                Tone_t first_tone = (mls_31_polar[0] == 1) ? TONE_D : TONE_A;
                dds_set_tone(&tx->dds, first_tone);
            }
            break;

        case STATE_PREAMBLE:
            dds_generate_sample(&tx->dds, out_i, out_q);
            tx->sample_counter++;

            // Конец текущего символа (256 сэмплов)
            if (tx->sample_counter >= SAMPLES_PER_SYMBOL) {
                tx->sample_counter = 0;
                tx->symbol_counter++;

                if (tx->symbol_counter >= 31) {
					// Преамбула отстрелялась. Проверяем, есть ли данные для отправки
					if (tx->raw_data_bytes && tx->total_bytes > 0) {
						tx->state = STATE_DATA;
						tx->symbol_counter = 0;

						// Извлекаем САМЫЙ ПЕРВЫЙ скремблированный дибит
						uint8_t first_scrambled_dibit = 0;
						scramble_iterate(tx->raw_data_bytes, tx->total_bytes, &tx->dibit_index, &first_scrambled_dibit);

						// Буквальный маппинг дибита в частоту сетки 4-MFSK
						dds_set_tone(&tx->dds, (Tone_t)(first_scrambled_dibit & 0x03));
					} else {
						// Если данных нет (только преамбула), идем в финальный пилот
						tx->state = STATE_TRAILER_PILOT;
						dds_set_tone(&tx->dds, TONE_A);
					}
				} else {
					Tone_t next_tone = (mls_31_polar[tx->symbol_counter] == 1) ? TONE_D : TONE_A;
					dds_set_tone(&tx->dds, next_tone);
				}
            }
            break;

        case STATE_DATA: // <-- Логика работы с полезной нагрузкой 4-MFSK/MDFSK
            dds_generate_sample(&tx->dds, out_i, out_q);
            tx->sample_counter++;

            if (tx->sample_counter >= SAMPLES_PER_SYMBOL) {
                tx->sample_counter = 0;
                tx->symbol_counter++;

                uint8_t next_scrambled_dibit = 0;
				// Запрашиваем у вашего итератора следующий дибит.
				// Он сам продвинет tx->dibit_index и обработает байты.
				int is_last_dibit = scramble_iterate(tx->raw_data_bytes, tx->total_bytes, &tx->dibit_index, &next_scrambled_dibit);

				if (is_last_dibit) {
					// Это был последний дибит пакета, на следующем символе переключаемся на хвостовой пилот
					tx->state = STATE_TRAILER_PILOT;
					dds_set_tone(&tx->dds, TONE_A);
				} else {
					// Штатный режим: мапим дибит в тон и едем дальше
					dds_set_tone(&tx->dds, (Tone_t)(next_scrambled_dibit & 0x03));
				}
            }
            break;


        case STATE_TRAILER_PILOT:
            dds_generate_sample(&tx->dds, out_i, out_q);
            tx->sample_counter++;

            // 0.5 секунды отладочного пилота = 4000 сэмплов
            if (tx->sample_counter >= (uint32_t)(0.5f * FS)) {
                tx->state = STATE_FINAL_SILENCE;
                tx->sample_counter = 0;
            }
            break;

        case STATE_FINAL_SILENCE:
            *out_i = 0.0f;
            *out_q = 0.0f;
            tx->sample_counter++;

            // 0.3 секунды финальной тишины = 2400 сэмплов
            if (tx->sample_counter >= (uint32_t)(0.3f * FS)) {
                tx->state = STATE_IDLE;
                tx->is_active = false;
            }
            break;

        default:
            *out_i = 0.0f;
            *out_q = 0.0f;
            return false;
    }

    return true;
}

// Сброс накопителей перед новым символом
void mfsk_demod_reset_symbol(mfsk_demod_t *demod) {
    for (int i = 0; i < 4; i++) {
        demod->integrators[i].re = 0.0f;
        demod->integrators[i].im = 0.0f;
    }
    demod->sample_idx = 0;
}

// Обработка одного сэмпла "на лету"
// Возвращает true, если символ завершен и решение готово
bool mfsk_demod_process_sample(mfsk_demod_t *demod, cplx_f32 in_sample, uint8_t *out_symbol) {
    uint32_t k = demod->sample_idx;

    // Интегрируем (ДПФ на выбранных частотах)
    // Используем float-тригонометрию (на ПК работает быстро, на МК у нас аппаратный Float)
    for (int n = 0; n < 4; n++) {
        float freq = TONE(n);
        // Угол для опорного генератора приемника
        float angle = -2.0f * M_PI * freq * (float)k / (float)FS;
        float cos_a = cosf(angle);
        float sin_a = sinf(angle);

        // Комплексное перемножение: in_sample * exp(-j*angle)
        // (re + j*im) * (cos_a + j*sin_a) = (re*cos_a - im*sin_a) + j*(re*sin_a + im*cos_a)
        demod->integrators[n].re += in_sample.re * cos_a - in_sample.im * sin_a;
        demod->integrators[n].im += in_sample.re * sin_a + in_sample.im * cos_a;
    }

    demod->sample_idx++;

    // Окно символа закрылось, принимаем решение
    if (demod->sample_idx >= SAMPLES_PER_SYMBOL) {
        float max_power = -1.0f;
        uint8_t best_tone = 0;

        for (uint8_t n = 0; n < 4; n++) {
            // Вычисляем мощность тона (квадрат модуля)
            float power = (demod->integrators[n].re * demod->integrators[n].re) +
                          (demod->integrators[n].im * demod->integrators[n].im);

            if (power > max_power) {
                max_power = power;
                best_tone = n;
            }
        }

        *out_symbol = best_tone;

        // Сбрасываемся для следующего символа
        mfsk_demod_reset_symbol(demod);
        return true; // Символ успешно демодулирован
    }

    return false; // Еще копим сэмплы
}
/*-----------------------------------------------------------------------------*/
void rx_sync_init(rx_sync_t *sync) {
    sync->state = SYNC_SEARCH_PILOT;
    sync->sample_idx = 0;
    sync->history_head = 0;
    sync->pilot_detect_counter = 0;
    sync->sync_sample_position = 0;
    for(int i=0; i < MLS31_LENGTH; i++) sync->mls_energy_history[i] = 0.0f;
}

#include <stdio.h>
extern FILE *csv;

// Вызывается для каждого входящего I/Q сэмпла из эфира
// global_sample_idx — сквозной счетчик сэмплов от начала файла/потока
// прим.: не работает в таком виде из-за слишком малого шага частот
bool rx_sync_process_sample(rx_sync_t *sync, cplx_f32 in_sample, uint32_t global_sample_idx) {
    uint32_t k = sync->sample_idx;
    float correlation = 0.0f;

    // 1. Вычисляем текущие корреляционные интегралы для двух частот преамбулы
    for (int n = 0; n < 2; n++) {
        float freq = TONE(n); // TONE_A (400) и TONE_B (431.25)
        float angle = -2.0f * M_PI * freq * (float)k / (float)FS;
        float cos_a = cosf(angle);
        float sin_a = sinf(angle);

        sync->integrators[n].re += in_sample.re * cos_a - in_sample.im * sin_a;
        sync->integrators[n].im += in_sample.re * sin_a + in_sample.im * cos_a;
    }

    sync->sample_idx++;

    // 2. Каждые 256 сэмплов (длительность одного символа) анализируем энергию
    if (sync->sample_idx >= SAMPLES_PER_SYMBOL) {
        float p_a = (sync->integrators[0].re * sync->integrators[0].re) + (sync->integrators[0].im * sync->integrators[0].im);
        float p_b = (sync->integrators[1].re * sync->integrators[1].re) + (sync->integrators[1].im * sync->integrators[1].im);

        // Сбрасываем интеграторы для следующего шага скольжения окна
        sync->integrators[0].re = 0.0f; sync->integrators[0].im = 0.0f;
        sync->integrators[1].re = 0.0f; sync->integrators[1].im = 0.0f;
        sync->sample_idx = 0;

        switch (sync->state) {
            case SYNC_SEARCH_PILOT:
                // Если энергия Тона А значительно превышает Тон Б и какой-то порог
                if (p_a > p_b * 4.0f && p_a > 0.01f) {
                    sync->pilot_detect_counter++;
                    // Если стабильно принимаем пилот на протяжении, например, 20 символов (~0.6 сек)
                    if (sync->pilot_detect_counter > 20) {
                        sync->state = SYNC_WAIT_PREAMBLE;
                    }
                } else {
                    if (sync->pilot_detect_counter > 0) sync->pilot_detect_counter--;
                }
                break;

            case SYNC_WAIT_PREAMBLE:
                // Записываем разность энергий в кольцевой буфер истории
                // Если был Тон B, значение будет строго положительным, если Тон А - отрицательным
                sync->mls_energy_history[sync->history_head] = p_b - p_a;

                // Считаем свертку (кросс-корреляцию) текущей истории с эталоном MLS-31
                correlation = 0.0f;
                for (int i = 0; i < MLS31_LENGTH; i++) {
                    int idx = (sync->history_head + 1 + i) % MLS31_LENGTH;
                    correlation += sync->mls_energy_history[idx] * (float)mls_31_polar[i];
                }

                // Продвигаем голову кольцевого буфера
                sync->history_head = (sync->history_head + 1) % MLS31_LENGTH;

                // Порог обнаружения пика. В идеальном канале значение взлетит до небес.
                // Для надежности фиксируем сильное превалирование корреляции
                if (correlation > 50.0f) { // Значение порога подбирается на чистом сигнале
                    sync->state = SYNC_LOCKED;
                    // Точка синхронизации найдена! Данные начинаются прямо со следующего сэмпла
                    sync->sync_sample_position = global_sample_idx + 1;
                    return true; // Синхронизация УСПЕШНО ЗАХВАЧЕНА
                }
                break;

            default:
                break;
        };
        const char* state_str = (sync->state == SYNC_SEARCH_PILOT) ? "SEARCH_PILOT" :
                                    (sync->state == SYNC_WAIT_PREAMBLE) ? "WAIT_PREAMBLE" : "LOCKED";

        fprintf(csv, "%u,%.2f,%s\n", global_sample_idx, correlation, state_str);
    }

    return false;
}

// Инициализация LO на частоту TONE_A (400 Гц)
void rx_lo_init(rx_lo_t *lo) {
    lo->phase_accumulator = 0;
    // step = (400 * 2^32) / 8000 = 214748365 (ровно 1/20 от 2^32)
    lo->phase_step = (uint32_t)((TONE_BASE * 4294967296.0) / FS);
}

// Получение комплексного значения гетеродина для текущего шага
cplx_f32 rx_lo_get_complex(rx_lo_t *lo) {
    // Используем ту же таблицу синуса на 1024 точки, что и в передатчике
    uint16_t index_i = (uint16_t)(lo->phase_accumulator >> 22) & 1023;
    uint16_t index_q = (index_i + 256) & 1023;

    cplx_f32 lo_val;
    lo_val.re = (float)sine_table[index_i] / 32767.0f;
    lo_val.im = (float)sine_table[index_q] / 32767.0f;

    // Продвигаем фазу гетеродина НЕПРЕРЫВНО, без сбросов
    lo->phase_accumulator += lo->phase_step;
    return lo_val;
}

// --- Новая функция поиска пилота ---
// Вызывается для каждого сэмпла. Больше никаких жестких окон по 256 сэмплов для пилота!
bool rx_search_pilot_efficient(rx_lo_t *lo, cplx_f32 in_sample, float *out_dc_power) {
    // 1. Перенос вниз (смещаем TONE_A в 0 Гц)
    cplx_f32 lo_sample = rx_lo_get_complex(lo);

    // Умножаем на сопряженное: истинный сдвиг частоты вниз
    cplx_f32 baseband_sample = cplx_mul_conj(in_sample, lo_sample);

    // 2. Интегратор-сглаживатель (простейший БИХ-фильтр низких частот / leaky integrator)
    // Он работает как огромный накопитель энергии на частоте 0 Гц
    static cplx_f32 dc_smoothed = {0.0f, 0.0f};

    // Коэффициент альфа задает инерционность (например, 0.995 для постоянного тока)
    float alpha = 0.995f;
    dc_smoothed.re = alpha * dc_smoothed.re + (1.0f - alpha) * baseband_sample.re;
    dc_smoothed.im = alpha * dc_smoothed.im + (1.0f - alpha) * baseband_sample.im;

    // Вычисляем текущую мощность постоянного тока
    *out_dc_power = (dc_smoothed.re * dc_smoothed.re) + (dc_smoothed.im * dc_smoothed.im);

    // Если мощность на 0 Гц превысила порог — пилот-тон обнаружен стабильно!
    // Поскольку сигнал чистый, значение уверенно прыгнет вверх
    if (*out_dc_power > 0.25f) {
        return true;
    }
    return false;
}
