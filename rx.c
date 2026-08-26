#include "rx.h"
#include "config.h"
#include <stdio.h>
#include <math.h>

static complex_f sync_buffer[N_SAMPLES];
static uint32_t sync_buf_idx = 0;
static complex_f data_buffer[N_SAMPLES + 5]; // Запас по памяти для растяжения такта

static rx_state_t rx_current_state = RX_STATE_SEARCH;
static float freq_offset_hz = 0.0f;

static complex_f P = {0.0f, 0.0f};
static float R_energy = 0.0f;

static uint32_t guard_sample_cnt = 0;
static uint32_t data_sample_cnt = 0;
static uint32_t samples_since_sync = 0;
static uint32_t samples_to_wait = 0;

// Переменные DPLL
static float dpll_error_accumulator = 0.0f;
static uint32_t current_target_data_samples = N_SAMPLES;

static complex_f prev_tone_integrals[NUM_DATA_TONES];
static bool rx_phases_initialized = false;
static uint32_t symbol_counter = 0;

static uint32_t search_holdoff_counter = 0;

void rx_init(void) {
    rx_current_state = RX_STATE_SEARCH;
    sync_buf_idx = 0; P.re = 0.0f; P.im = 0.0f; R_energy = 0.0f;
    freq_offset_hz = 0.0f; guard_sample_cnt = 0; data_sample_cnt = 0; samples_since_sync = 0;
    samples_to_wait = 0; dpll_error_accumulator = 0.0f; current_target_data_samples = N_SAMPLES;
    rx_phases_initialized = false; symbol_counter = 0;
    search_holdoff_counter = 0;
    for (int i = 0; i < N_SAMPLES; i++) {
        sync_buffer[i].re = 0.0f; sync_buffer[i].im = 0.0f;
        data_buffer[i].re = 0.0f; data_buffer[i].im = 0.0f;
    }
    printf("[FSM Init] Автомат сброшен. Состояние: RX_STATE_SEARCH. Ожидание сигнала...\n");
}

float rx_get_frequency_offset(void) { return freq_offset_hz; }

bool rx_process_iq_sample(int16_t sample_i, int16_t sample_q, uint8_t *out_nibble) {
    complex_f x_curr;
    x_curr.re = (float)sample_i / 32768.0f;
    x_curr.im = (float)sample_q / 32768.0f;

    if (rx_current_state != RX_STATE_SEARCH) {
        samples_since_sync++;
    }

    switch (rx_current_state) {

        case RX_STATE_SEARCH: {
            if (search_holdoff_counter > 0) {
                search_holdoff_counter--;

                // Обязательно обновляем кольцевой буфер, чтобы вытеснить старый сигнал
                sync_buffer[sync_buf_idx] = x_curr;
                sync_buf_idx = (sync_buf_idx + 1) % N_SAMPLES;
                break;
            }
            uint32_t idx_half = (sync_buf_idx + HALF_N) % N_SAMPLES;
            uint32_t idx_full = sync_buf_idx;

            complex_f x_half = sync_buffer[idx_half];
            complex_f x_full = sync_buffer[idx_full];

            sync_buffer[sync_buf_idx] = x_curr;
            sync_buf_idx = (sync_buf_idx + 1) % N_SAMPLES;

            float curr_conj_half_re = x_curr.re * x_half.re + x_curr.im * x_half.im;
            float curr_conj_half_im = x_curr.im * x_half.re - x_curr.re * x_half.im;
            float half_conj_full_re = x_half.re * x_full.re + x_half.im * x_full.im;
            float half_conj_full_im = x_half.im * x_full.re - x_half.re * x_full.im;

            P.re += curr_conj_half_re - half_conj_full_re;
            P.im += curr_conj_half_im - half_conj_full_im;
            R_energy += (x_half.re * x_half.re + x_half.im * x_half.im) - (x_full.re * x_full.re + x_full.im * x_full.im);

            if (R_energy > 0.01f) {
                float metric = (P.re * P.re + P.im * P.im) / (R_energy * R_energy);

                if (metric > 0.80f) {
                    float phase_diff = atan2f(P.im, P.re);
                    freq_offset_hz = phase_diff / (2.0f * PI_F * ((float)HALF_N / FS));

                    // Жесткий расчет до конца преамбулы + первый CP
                    samples_to_wait = (2 * N_SAMPLES - HALF_N) + CP_SAMPLES;

                    printf("\n[FSM Детекция] Порог Шмидла-Кокса превышен! Метрика: %.3f\n", metric);
                    printf("[FSM Детекция] Рассчитанный КВ-дрифт частоты: %.2f Гц\n", freq_offset_hz);
                    printf("[FSM Переход] SEARCH -> GUARD. Ожидание паузы: %u сэмплов.\n", samples_to_wait);

                    guard_sample_cnt = 0;
                    samples_since_sync = 0;
                    rx_current_state = RX_STATE_GUARD;
                }
            }
            break;
        }

        case RX_STATE_GUARD: {
            guard_sample_cnt++;
            if (guard_sample_cnt >= samples_to_wait) {
                data_sample_cnt = 0;
                rx_current_state = RX_STATE_DECODE;
                // Фазовая компенсация для ДПФ-окна всегда стартует локально с нуля
                samples_since_sync = 0;
            }
            break;
        }

        case RX_STATE_DECODE: {
            // Компенсация частоты гетеродина КВ на лету
            float comp_t = (float)samples_since_sync / FS;
            float comp_phase = -2.0f * PI_F * freq_offset_hz * comp_t;

            complex_f x_comp;
            x_comp.re = x_curr.re * cosf(comp_phase) - x_curr.im * sinf(comp_phase);
            x_comp.im = x_curr.re * sinf(comp_phase) + x_curr.im * cosf(comp_phase);

            data_buffer[data_sample_cnt] = x_comp;
            data_sample_cnt++;

            // Слушаем динамическое окно, заданное DPLL на прошлом шаге!
            if (data_sample_cnt >= current_target_data_samples) {
                uint8_t decoded_nibble = 0;
                float total_timing_phase_error = 0.0f;
                symbol_counter++;

                printf("\n--- АНАЛИЗ СИМВОЛА ПОТОКА №%u (Взято отсчетов АЦП: %u) ---\n",
                       symbol_counter, current_target_data_samples);

                // Вычисляем ДПФ строго по ортогональной сетке N_SAMPLES (160)
                for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
                    float tone_freq = data_tones[tone];
                    complex_f curr_tone_integral = {0.0f, 0.0f};

                    for (int i = 0; i < N_SAMPLES; i++) {
                        complex_f clean_sample = data_buffer[i];
                        float t_window = (float)i / FS;
                        float cos_ref = cosf(2.0f * PI_F * tone_freq * t_window);
                        float sin_ref = sinf(2.0f * PI_F * tone_freq * t_window);

                        curr_tone_integral.re += clean_sample.re * cos_ref + clean_sample.im * sin_ref;
                        curr_tone_integral.im += clean_sample.im * cos_ref - clean_sample.re * sin_ref;
                    }

                    if (!rx_phases_initialized) {
                        prev_tone_integrals[tone] = curr_tone_integral;
                    } else {
                        float dot_product_re = curr_tone_integral.re * prev_tone_integrals[tone].re +
                                               curr_tone_integral.im * prev_tone_integrals[tone].im;
                        float dot_product_im = curr_tone_integral.im * prev_tone_integrals[tone].re -
                                               curr_tone_integral.re * prev_tone_integrals[tone].im;

                        if (dot_product_re < 0.0f) {
                            decoded_nibble |= (1 << tone);
                        }

                        // Дискриминатор DPLL: оцениваем косину и фазовый увод вектора
                        float tone_error = dot_product_im;
                        if (dot_product_re < 0.0f) {
                            tone_error = -tone_error;
                        }
                        total_timing_phase_error += tone_error * (float)(tone + 1);

                        printf(" Тон %d (%4.0f Гц): ДПФ=[%6.2f, j(%6.2f)] | DPSK_RE=%6.2f, IM=%6.2f | БИТ=%d\n",
                               tone, tone_freq, curr_tone_integral.re, curr_tone_integral.im,
                               dot_product_re, dot_product_im, (dot_product_re < 0.0f) ? 1 : 0);

                        prev_tone_integrals[tone] = curr_tone_integral;
                    }
                }

                data_sample_cnt = 0;

                // РАБОТА ПЕТЛИ DPLL И ВЫБОР ШАГА СЛЕДУЮЩЕГО КАДРА
                if (rx_phases_initialized) {
                    dpll_error_accumulator += total_timing_phase_error * 0.02f; // Коэффициент удержания петли
                    printf("[DPLL Телеметрия] Суммарная ошибка фазы: %.3f | Интегратор петли: %.3f\n",
                           total_timing_phase_error, dpll_error_accumulator);

                    if (dpll_error_accumulator > 1.0f) {
                        // Приемник бежит быстрее передатчика -> укорачиваем следующий символ, беря меньше отсчетов АЦП
                        current_target_data_samples = N_SAMPLES - 1;
                        samples_to_wait = CP_SAMPLES;
                        dpll_error_accumulator = 0.0f;
                        printf("[DPLL Автоматика] !!! КОРРЕКЦИЯ: ТАКТ СЖАТ ДО %u СЭМПЛОВ !!!\n", current_target_data_samples);
                    }
                    else if (dpll_error_accumulator < -1.0f) {
                        // Приемник отстает -> удлиняем следующий шаг
                        current_target_data_samples = N_SAMPLES + 1;
                        samples_to_wait = CP_SAMPLES;
                        dpll_error_accumulator = 0.0f;
                        printf("[DPLL Автоматика] !!! КОРРЕКЦИЯ: ТАКТ РАСШИРЕН ДО %u СЭМПЛОВ !!!\n", current_target_data_samples);
                    }
                    else {
                        // Шагаем по стандартной сетке кадра
                        current_target_data_samples = N_SAMPLES;
                        samples_to_wait = CP_SAMPLES;
                    }
                } else {
                    // Обработали первый ("гарпунный") символ
                    printf("[DPSK Базис] 'Гарпунный' символ успешно захвачен и сохранен как фазовая опора кадра.\n");
                    rx_phases_initialized = true;
                    current_target_data_samples = N_SAMPLES;
                    samples_to_wait = CP_SAMPLES;
                }

                if (decoded_nibble != 0x0F) {
                	printf ("Decoded nibble: 0x%X\n", decoded_nibble);
                } else {
                	printf ("EOT detected\n");
                }

                // Проверка флага завершения передачи по маркеру EOT
                if (rx_phases_initialized && decoded_nibble == 0x0F) {
                    printf("[FSM Завершение] Получен маркер EOT. Поток остановлен.\n");
                    rx_current_state = RX_STATE_SEARCH;
                    rx_phases_initialized = false;

                    // Запрещаем Шмидлу-Коксу работать следующие 800 сэмплов (100 мс тишины)
                    search_holdoff_counter = 800;
                    return true;
                }

                // Шагаем на следующий символ потока через пропуск CP
                guard_sample_cnt = 0;
                rx_current_state = RX_STATE_GUARD;
                return rx_phases_initialized; // Возвращаем true только для боевых букв
                }
            break;
        }
    }
    return false;
}
