#include "rx.h"
#include "config.h"
#include <stdio.h>
#include <math.h>

static complex_f data_buffer[TOTAL_SYMBOL_SAMPLES];
static uint32_t rx_global_timer = 0;
static uint32_t sync_absolute_tick = 0;

static rx_state_t rx_current_state = RX_STATE_SEARCH;
static float freq_offset_hz = 0.0f;
static complex_f prev_tone_integrals[NUM_DATA_TONES];
static float preamble_freq_stats[PREAMBLE_SYMBOLS];
static uint32_t preamble_symbol_cnt = 0;

void rx_init(void) {
    rx_current_state = RX_STATE_SEARCH;
    rx_global_timer = 0;
    sync_absolute_tick = 0;
    freq_offset_hz = 0.0f;
    preamble_symbol_cnt = 0;
    for(int i=0; i<TOTAL_SYMBOL_SAMPLES; i++) { data_buffer[i].re = 0.0f; data_buffer[i].im = 0.0f; }
    for(int i=0; i<NUM_DATA_TONES; i++) { prev_tone_integrals[i].re = 0.0f; prev_tone_integrals[i].im = 0.0f; }
    printf("[Приемник] Режим SEARCH: Интегральный 16-сэмпловый дискриминатор скачков запущен.\n");
}

// Прямой расчет ДПФ по локальному окну буфера длиной W_LEN (16 сэмплов)
void calc_sliding_dft(complex_f *buf_ptr, int w_len, complex_f *results) {
    for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
        float tone_freq = data_tones[tone];
        results[tone].re = 0.0f;
        results[tone].im = 0.0f;

        for (int i = 0; i < w_len; i++) {
            float t = (float)i / FS;
            float cos_ref = cosf(2.0f * PI_F * tone_freq * t);
            float sin_ref = sinf(2.0f * PI_F * tone_freq * t);

            results[tone].re += buf_ptr[i].re * cos_ref + buf_ptr[i].im * sin_ref;
            results[tone].im += buf_ptr[i].im * cos_ref - buf_ptr[i].re * sin_ref;
        }
    }
}

// Восстановленная точная глобальная ДПФ для этапа калибровки
void execute_global_dft(complex_f *buffer, uint32_t start_time, complex_f *results) {
    for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
        float tone_freq = data_tones[tone];
        results[tone].re = 0.0f; results[tone].im = 0.0f;
        for (int i = 0; i < N_SAMPLES; i++) {
            uint32_t current_tick = start_time + i;
            float t_global = (float)current_tick / FS;
            float cos_ref = cosf(2.0f * PI_F * tone_freq * t_global);
            float sin_ref = sinf(2.0f * PI_F * tone_freq * t_global);
            results[tone].re += buffer[i].re * cos_ref + buffer[i].im * sin_ref;
            results[tone].im += buffer[i].im * cos_ref - buffer[i].re * sin_ref;
        }
    }
}

bool rx_process_sample(complex_f *sample, uint8_t *out_nibble) {
    complex_f x_curr = *sample;
    rx_global_timer++;

    // Скользящее окно кадра данных
    for(int i = 0; i < TOTAL_SYMBOL_SAMPLES - 1; i++) data_buffer[i] = data_buffer[i+1];
    data_buffer[TOTAL_SYMBOL_SAMPLES - 1] = x_curr;

    switch (rx_current_state) {

        case RX_STATE_SEARCH: {
            // Анализируем хвост буфера только когда он заполнился хотя бы на 32 сэмпла
            if (rx_global_timer < 32) break;

            complex_f dft_win_A[NUM_DATA_TONES]; // Опережающее окно (последние 16 сэмплов)
            complex_f dft_win_B[NUM_DATA_TONES]; // Запаздывающее окно (предыдущие 16 сэмплов)

            int offset_A = TOTAL_SYMBOL_SAMPLES - 16;
            int offset_B = TOTAL_SYMBOL_SAMPLES - 32;

            calc_sliding_dft(&data_buffer[offset_A], 16, dft_win_A);
            calc_sliding_dft(&data_buffer[offset_B], 16, dft_win_B);

            uint32_t active_jumps = 0;

            for (int t = 0; t < NUM_DATA_TONES; t++) {
                float magnitude_A_sq = dft_win_A[t].re * dft_win_A[t].re + dft_win_A[t].im * dft_win_A[t].im;

                // Проверяем энергетический порог присутствия сигнала в окне ДПФ,
                // чтобы полностью исключить atan2f(0,0) в чистой тишине!
                if (magnitude_A_sq > 0.05f) {
                    float dot_re = dft_win_A[t].re * dft_win_B[t].re + dft_win_A[t].im * dft_win_B[t].im;
                    float dot_im = dft_win_A[t].im * dft_win_B[t].re - dft_win_A[t].re * dft_win_B[t].im;

                    float diff_phase = fabsf(atan2f(dot_im, dot_re));

                    // Если разность фаз между окнами близка к квадратурным 90 градусам (1.57 +- 0.4 радиан)
                    if (diff_phase > 1.1f && diff_phase < 2.0f) {
                        active_jumps++;
                    }
                }
            }

            // КРИТЕРИЙ ИСТИННОГО СКАЧКА: Все 4 дискриминатора одновременно зафиксировали
            // квадратурный переход фаз над накопленным сигналом!
            if (active_jumps == NUM_DATA_TONES) {
                // Вычисляем точную физическую точку щелчка фазы передатчика:
                // Она находится ровно на стыке окон А и Б (16 сэмплов назад от текущего момента таймера)
                sync_absolute_tick = rx_global_timer - 16;
                preamble_symbol_cnt = 0;

                printf("\n[DPLL Крюк] Квадратурный скачок фаз подтвержден на сэмпле #%u!\n", sync_absolute_tick);
                rx_current_state = RX_STATE_PREAMBLE_CALIBRATE;
            }
            break;
        }

        case RX_STATE_PREAMBLE_CALIBRATE: {
            uint32_t elapsed_since_sync = rx_global_timer - sync_absolute_tick;

            // Ждем наполнения кадра от точки щелчка
            if (elapsed_since_sync > 0 && elapsed_since_sync % TOTAL_SYMBOL_SAMPLES == 0) {
                complex_f dft_res[NUM_DATA_TONES];
                uint32_t dft_start_tick = rx_global_timer - N_SAMPLES;
                execute_global_dft(&data_buffer[CP_SAMPLES], dft_start_tick, dft_res);

                if (preamble_symbol_cnt == 0) {
                    for(int t=0; t<NUM_DATA_TONES; t++) prev_tone_integrals[t] = dft_res[t];
                    preamble_symbol_cnt++;
                    printf("[Преамбула] Шаг %u: Фазовый базис сохранен.\n", preamble_symbol_cnt);
                } else {
                    float total_phase_deviation = 0.0f;
                    uint8_t expected_pattern = (preamble_symbol_cnt % 2 == 0) ? PREAMBLE_PATTERN : (uint8_t)(~PREAMBLE_PATTERN & 0x0F);

                    for(int t=0; t<NUM_DATA_TONES; t++) {
                        float dot_re = dft_res[t].re * prev_tone_integrals[t].re + dft_res[t].im * prev_tone_integrals[t].im;
                        float dot_im = dft_res[t].im * prev_tone_integrals[t].re - dft_res[t].re * prev_tone_integrals[t].im;

                        float angle = atan2f(dot_im, dot_re);
                        float ideal_step = ((expected_pattern >> t) & 1) ? 1.570796f : -1.570796f;

                        total_phase_deviation += (angle - ideal_step);
                        prev_tone_integrals[t] = dft_res[t];
                    }

                    float symbol_freq_error = (total_phase_deviation / NUM_DATA_TONES) / (2.0f * PI_F * ((float)TOTAL_SYMBOL_SAMPLES / FS));
                    preamble_freq_stats[preamble_symbol_cnt] = symbol_freq_error;

                    printf("[Статистика] Шаг %u (Абс. сэмпл %u): Измеренный КВ-сдвиг частоты = %6.2f Гц\n",
                           preamble_symbol_cnt + 1, rx_global_timer, symbol_freq_error);

                    preamble_symbol_cnt++;
                }

                if (preamble_symbol_cnt >= PREAMBLE_SYMBOLS) {
                    float avg_drift = 0.0f;
                    for(uint32_t s=1; s<PREAMBLE_SYMBOLS; s++) avg_drift += preamble_freq_stats[s];
                    freq_offset_hz = avg_drift / (PREAMBLE_SYMBOLS - 1);

                    printf("\n=== ОКОНЧАТЕЛЬНЫЙ ВЕРДИКТ КАЛИБРОВКИ ===\n");
                    printf("-> Прецизионный КВ-дрейф частоты гетеродина: %.2f Гц\n", freq_offset_hz);
                    printf("-> Тактовая база выровнена. Включаем полезную нагрузку!\n\n");

                    rx_current_state = RX_STATE_DECODE;
                }
            }
            break;
        }

        default:
            break;
    }
    return false;
}
