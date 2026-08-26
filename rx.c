#include "rx.h"
#include "config.h"
#include <math.h>

static complex_f rx_buffer[N_SAMPLES];
static uint32_t buf_idx = 0;

static rx_state_t rx_current_state = RX_STATE_SEARCH;
static float freq_offset_hz = 0.0f;

static complex_f P = {0.0f, 0.0f};
static float R_energy = 0.0f;

static uint8_t preamble_detected = 0;

static uint32_t guard_sample_cnt = 0;
static uint32_t data_sample_cnt = 0;

// Убираем глобальный счетчик post_sync_sample_idx, заменяем на локальный для символа
static uint32_t current_symbol_sample_idx = 0;

static complex_f prev_tone_integrals[NUM_DATA_TONES];
static bool rx_phases_initialized = false;

void rx_init(void) {
    rx_current_state = RX_STATE_SEARCH;
    buf_idx = 0;
    P.re = 0.0f; P.im = 0.0f;
    R_energy = 0.0f;
    freq_offset_hz = 0.0f;
    guard_sample_cnt = 0;
    data_sample_cnt = 0;
    current_symbol_sample_idx = 0;
    preamble_detected = 0;
    rx_phases_initialized = false;
    for (int i = 0; i < N_SAMPLES; i++) {
        rx_buffer[i].re = 0.0f;
        rx_buffer[i].im = 0.0f;
    }
}

float rx_get_frequency_offset(void) {
    return freq_offset_hz;
}

bool rx_process_iq_sample(int16_t sample_i, int16_t sample_q, uint8_t *out_nibble) {
    complex_f x_curr;
    x_curr.re = (float)sample_i / 32768.0f;
    x_curr.im = (float)sample_q / 32768.0f;

    uint32_t idx_half = (buf_idx + HALF_N) % N_SAMPLES;
    uint32_t idx_full = buf_idx;

    complex_f x_half = rx_buffer[idx_half];
    complex_f x_full = rx_buffer[idx_full];

    rx_buffer[buf_idx] = x_curr;
    uint32_t current_write_pos = buf_idx;
    buf_idx = (buf_idx + 1) % N_SAMPLES;

    switch (rx_current_state) {

        case RX_STATE_SEARCH: {
            float curr_conj_half_re = x_curr.re * x_half.re + x_curr.im * x_half.im;
            float curr_conj_half_im = x_curr.im * x_half.re - x_curr.re * x_half.im;

            float half_conj_full_re = x_half.re * x_full.re + x_half.im * x_full.im;
            float half_conj_full_im = x_half.im * x_full.re - x_half.re * x_full.im;

            P.re += curr_conj_half_re - half_conj_full_re;
            P.im += curr_conj_half_im - half_conj_full_im;

            R_energy += (x_half.re * x_half.re + x_half.im * x_half.im) -
                        (x_full.re * x_full.re + x_full.im * x_full.im);

            if (R_energy > 0.01f) {
                float p_magnitude_sq = P.re * P.re + P.im * P.im;
                float metric = p_magnitude_sq / (R_energy * R_energy);

                if (metric > 0.90f) {
                    if (!preamble_detected) {
                        // Измеряем частоту один раз в момент стабильного сигнала
                        float phase_diff = atan2f(P.im, P.re);
                        freq_offset_hz = phase_diff / (2.0f * PI_F * ((float)HALF_N / FS));
                        preamble_detected = 1;
                    }
                } else if (preamble_detected && metric < 0.70f) {
                    // Метрика упала! Преамбула гарантированно закончилась.
                    // Прямо сейчас в эфире начинается первый сэмпл циклического префикса данных!
                    guard_sample_cnt = 0;
                    current_symbol_sample_idx = 0;
                    rx_current_state = RX_STATE_GUARD; // Переходим к отсчету префикса данных
                }
            }
        }; break;

        case RX_STATE_GUARD: {
            guard_sample_cnt++;
            current_symbol_sample_idx++; // Время идет и во время префикса

            if (guard_sample_cnt >= CP_SAMPLES) {
                data_sample_cnt = 0;
                rx_current_state = RX_STATE_DECODE;
            }
        }; break;

        case RX_STATE_DECODE: {
            data_sample_cnt++;
            current_symbol_sample_idx++;

            // Инициализируем опорные фазы нулями, если это самый первый символ после синхронизации
            if (!rx_phases_initialized) {
                for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
                    prev_tone_integrals[tone].re = 1.0f; // Опорный вектор «смотрим в плюс»
                    prev_tone_integrals[tone].im = 0.0f;
                }
                rx_phases_initialized = true;
            }

            if (data_sample_cnt >= N_SAMPLES) {
                uint8_t decoded_nibble = 0;

                for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
                    float tone_freq = data_tones[tone];
                    complex_f curr_tone_integral = {0.0f, 0.0f};

                    for (int i = 0; i < N_SAMPLES; i++) {
                        uint32_t read_idx = (current_write_pos + 1 + i) % N_SAMPLES;
                        complex_f clean_sample = rx_buffer[read_idx];

                        float t_window = (float)i / FS;
                        float re_ref = cosf(2.0f * PI_F * tone_freq * t_window);
                        float im_ref = sinf(2.0f * PI_F * tone_freq * t_window);

                        curr_tone_integral.re += clean_sample.re * re_ref + clean_sample.im * im_ref;
                        curr_tone_integral.im += clean_sample.im * re_ref - clean_sample.re * im_ref;
                    }

                    // ДИФФЕРЕНЦИАЛЬНОЕ РЕШАЮЩЕЕ ПРАВИЛО (DPSK)
                    // Находим скалярное произведение текущего интеграла тона и предыдущего
                    // Вычисляем вещественную часть: Real(Curr * Conj(Prev))
                    float dot_product_re = curr_tone_integral.re * prev_tone_integrals[tone].re +
                                           curr_tone_integral.im * prev_tone_integrals[tone].im;

                    if (dot_product_re < 0.0f) {
                        decoded_nibble |= (1 << tone); // Набег фазы 180 градусов -> это бит 1
                    }

                    // Сохраняем текущий интеграл как базу для следующего символа потока данных
                    prev_tone_integrals[tone] = curr_tone_integral;
                }

                *out_nibble = decoded_nibble;
                data_sample_cnt = 0;
                current_symbol_sample_idx = 0; // Сброс для следующего символа потока

                // В БОЕВОМ режиме здесь должен быть переход в RX_STATE_GUARD для следующего символа данных,
                // но пока для теста одиночного пакета вернемся в SEARCH
                rx_current_state = RX_STATE_SEARCH;
                preamble_detected = 0;
                rx_phases_initialized = true; // Для одиночного теста сбрасываем для следующего прогона
                return true;
            }
        }; break;
    }

    return false;
}
