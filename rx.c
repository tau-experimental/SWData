#include "rx.h"
#include "config.h"
#include <math.h>

static complex_f rx_buffer[N_SAMPLES];
static uint32_t buf_idx = 0;

static rx_state_t rx_current_state = RX_STATE_SEARCH;
static float freq_offset_hz = 0.0f;

static complex_f P = {0.0f, 0.0f};
static float R_energy = 0.0f;

static uint32_t guard_sample_cnt = 0;
static uint32_t data_sample_cnt = 0;
static uint32_t samples_since_sync = 0;

// Переменные для поиска пика Шмидла-Кокса
static float max_metric = 0.0f;
static uint32_t peak_search_counter = 0;
static uint32_t samples_to_data_start = 0;
static uint32_t sample_counter_since_threshold = 0;
static uint32_t peak_location_from_threshold = 0;

static complex_f prev_tone_integrals[NUM_DATA_TONES];
static bool rx_phases_initialized = false;

void rx_init(void) {
    rx_current_state = RX_STATE_SEARCH;
    buf_idx = 0; P.re = 0.0f; P.im = 0.0f; R_energy = 0.0f;
    freq_offset_hz = 0.0f; guard_sample_cnt = 0; data_sample_cnt = 0; samples_since_sync = 0;
    max_metric = 0.0f; peak_search_counter = 0; samples_to_data_start = 0;
    sample_counter_since_threshold = 0;
    peak_location_from_threshold = 0;
    rx_phases_initialized = false;
    for (int i = 0; i < N_SAMPLES; i++) { rx_buffer[i].re = 0.0f; rx_buffer[i].im = 0.0f; }
}

float rx_get_frequency_offset(void) { return freq_offset_hz; }

bool rx_process_iq_sample(int16_t sample_i, int16_t sample_q, uint8_t *out_nibble) {
    complex_f x_curr;
    x_curr.re = (float)sample_i / 32768.0f;
    x_curr.im = (float)sample_q / 32768.0f;

    if (rx_current_state != RX_STATE_SEARCH && rx_current_state != RX_STATE_PEAK_HOLD) {
        samples_since_sync++;
    }

    uint32_t idx_half = (buf_idx + HALF_N) % N_SAMPLES;
    uint32_t idx_full = buf_idx;

    complex_f x_half = rx_buffer[idx_half];
    complex_f x_full = rx_buffer[idx_full];

    // В режимах поиска пика пишем сырые данные в буфер
    if (rx_current_state == RX_STATE_SEARCH || rx_current_state == RX_STATE_PEAK_HOLD) {
        rx_buffer[buf_idx] = x_curr;
    }
    uint32_t current_write_pos = buf_idx;
    buf_idx = (buf_idx + 1) % N_SAMPLES;

    switch (rx_current_state) {

        case RX_STATE_SEARCH: {
            // Рекурсивный расчет Шмидла-Кокса
            float curr_conj_half_re = x_curr.re * x_half.re + x_curr.im * x_half.im;
            float curr_conj_half_im = x_curr.im * x_half.re - x_curr.re * x_half.im;
            float half_conj_full_re = x_half.re * x_full.re + x_half.im * x_full.im;
            float half_conj_full_im = x_half.im * x_full.re - x_half.re * x_full.im;

            P.re += curr_conj_half_re - half_conj_full_re;
            P.im += curr_conj_half_im - half_conj_full_im;
            R_energy += (x_half.re * x_half.re + x_half.im * x_half.im) - (x_full.re * x_full.re + x_full.im * x_full.im);

            if (R_energy > 0.01f) {
                float metric = (P.re * P.re + P.im * P.im) / (R_energy * R_energy);

                // Порог первичного обнаружения сигнала
                if (metric > 0.75f) {
                    max_metric = metric;
                    sample_counter_since_threshold = 0;
                    peak_location_from_threshold = 0;

                    // Сразу вычисляем частоту первого приближения
                    float phase_diff = atan2f(P.im, P.re);
                    freq_offset_hz = phase_diff / (2.0f * PI_F * ((float)HALF_N / FS));

                    rx_current_state = RX_STATE_PEAK_HOLD;
                }
            }
            break;
        }

        case RX_STATE_PEAK_HOLD: {
            // Продолжаем крутить Шмидла-Кокса внутри окна поиска максимума
            float curr_conj_half_re = x_curr.re * x_half.re + x_curr.im * x_half.im;
            float curr_conj_half_im = x_curr.im * x_half.re - x_curr.re * x_half.im;
            float half_conj_full_re = x_half.re * x_full.re + x_half.im * x_full.im;
            float half_conj_full_im = x_half.im * x_full.re - x_half.re * x_full.im;

            P.re += curr_conj_half_re - half_conj_full_re;
            P.im += curr_conj_half_im - half_conj_full_im;
            R_energy += (x_half.re * x_half.re + x_half.im * x_half.im) - (x_full.re * x_full.re + x_full.im * x_full.im);

            if (R_energy > 0.01f) {
                float metric = (P.re * P.re + P.im * P.im) / (R_energy * R_energy);

                // Если метрика растет, обновляем пик и точное значение частоты
                if (metric > max_metric) {
                    max_metric = metric;
                    peak_location_from_threshold = sample_counter_since_threshold;

                    // Обновляем точную КВ-частоту в точке наилучшего сигнала
                    float phase_diff = atan2f(P.im, P.re);
                    freq_offset_hz = phase_diff / (2.0f * PI_F * ((float)HALF_N / FS));
                }
            }

            // Ищем пик на протяжении 240 сэмплов (это гарантированно покроет плато двух символов преамбулы)
            if (sample_counter_since_threshold >= 240) {
                guard_sample_cnt = 0;
                samples_since_sync = 0;

                // МАТЕМАТИЧЕСКИЙ РАСЧЕТ ТОЧКИ СТАРТА ДАННЫХ:
                // Истинный пик плато Шмидла-Кокса находится ровно в точке,
                // когда до конца преамбулы остается HALF_N (80) сэмплов.
                // Зная, сколько сэмплов прошло с момента порога до конца окна поиска (sample_counter_since_threshold = 240),
                // и где был пик (peak_location_from_threshold), вычисляем точный остаток пути до кадра данных:
                int32_t remainder = (int32_t)peak_location_from_threshold + HALF_N - (int32_t)sample_counter_since_threshold;

                // Если по шумам мы немного промахнулись, страхуем индекс, чтобы он не стал отрицательным
                if (remainder < 0) remainder = 0;
                samples_to_data_start = (uint32_t)remainder;

                rx_current_state = RX_STATE_GUARD;
            }
            break;
        }

        case RX_STATE_GUARD: {
            guard_sample_cnt++;

            // Компенсируем частоту на лету для залетающих сэмплов
            float comp_t = (float)samples_since_sync / FS;
            float comp_phase = -2.0f * PI_F * freq_offset_hz * comp_t;
            complex_f x_comp;
            x_comp.re = x_curr.re * cosf(comp_phase) - x_curr.im * sinf(comp_phase);
            x_comp.im = x_curr.re * sinf(comp_phase) + x_curr.im * cosf(comp_phase);

            rx_buffer[buf_idx] = x_comp;
            buf_idx = (buf_idx + 1) % N_SAMPLES;

            // Нам нужно пропустить остаток преамбулы (samples_to_data_start) + циклический префикс (CP_SAMPLES)
            if (guard_sample_cnt >= (samples_to_data_start + CP_SAMPLES)) {
                data_sample_cnt = 0;
                rx_current_state = RX_STATE_DECODE;
            }
            break;
        }

        case RX_STATE_DECODE: {
            data_sample_cnt++;

            float comp_t = (float)samples_since_sync / FS;
            float comp_phase = -2.0f * PI_F * freq_offset_hz * comp_t;
            complex_f x_comp;
            x_comp.re = x_curr.re * cosf(comp_phase) - x_curr.im * sinf(comp_phase);
            x_comp.im = x_curr.re * sinf(comp_phase) + x_comp.im * cosf(comp_phase);

            rx_buffer[buf_idx] = x_comp;
            uint32_t current_write_pos = buf_idx;
            buf_idx = (buf_idx + 1) % N_SAMPLES;

            if (data_sample_cnt >= N_SAMPLES) {
                uint8_t decoded_nibble = 0;

                printf("\n=== ПОДРОБНАЯ ДИАГНОСТИКА ДПФ (Кадр Данных) ===\n");
                printf("Измеренный дрифт для компенсации: %.2f Гц\n", freq_offset_hz);
                printf("Индекс записи буфера (current_write_pos): %u\n", current_write_pos);

                // Если это самый первый символ, покажем как инициализируется предыстория
                if (!rx_phases_initialized) {
                    printf("[DPSK] Первый запуск. Инициализация фазового базиса условным нулем.\n");
                }

                for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
                    float tone_freq = data_tones[tone];
                    complex_f curr_tone_integral = {0.0f, 0.0f};

                    for (int i = 0; i < N_SAMPLES; i++) {
                        uint32_t read_idx = (current_write_pos + 1 + i) % N_SAMPLES;
                        complex_f clean_sample = rx_buffer[read_idx];

                        float t_window = (float)i / FS;
                        float cos_ref = cosf(2.0f * PI_F * tone_freq * t_window);
                        float sin_ref = sinf(2.0f * PI_F * tone_freq * t_window);

                        curr_tone_integral.re += clean_sample.re * cos_ref + clean_sample.im * sin_ref;
                        curr_tone_integral.im += clean_sample.im * cos_ref - clean_sample.re * sin_ref;
                    }

                    if (!rx_phases_initialized) {
                        prev_tone_integrals[tone].re = 1.0f; // Принудительный вектор "вправо" для теста одного символа
                        prev_tone_integrals[tone].im = 0.0f;
                    }

                    // Комплексное скалярное произведение Z = Curr * conj(Prev)
                    float dot_product_re = curr_tone_integral.re * prev_tone_integrals[tone].re +
                                           curr_tone_integral.im * prev_tone_integrals[tone].im;
                    float dot_product_im = curr_tone_integral.im * prev_tone_integrals[tone].re -
                                           curr_tone_integral.re * prev_tone_integrals[tone].im;

                    // Определяем бит
                    uint8_t decoded_bit = (dot_product_re < 0.0f) ? 1 : 0;
                    if (decoded_bit) {
                        decoded_nibble |= (1 << tone);
                    }

                    // Выводим геометрию векторов в консоль
                    printf("Тон %d (%.0f Гц): \n", tone, tone_freq);
                    printf("  -> Текущий вектор ДПФ : [%.4f, j(%.4f)] | Модуль: %.4f\n",
                           curr_tone_integral.re, curr_tone_integral.im,
                           sqrtf(curr_tone_integral.re*curr_tone_integral.re + curr_tone_integral.im*curr_tone_integral.im));
                    printf("  -> Базисный вектор     : [%.4f, j(%.4f)]\n",
                           prev_tone_integrals[tone].re, prev_tone_integrals[tone].im);
                    printf("  -> Скалярное произв.   : RE = %.4f, IM = %.4f -> БИТ = %d\n",
                           dot_product_re, dot_product_im, decoded_bit);

                    // Сохраняем базу
                    prev_tone_integrals[tone] = curr_tone_integral;
                }

                rx_phases_initialized = true;
                *out_nibble = decoded_nibble;

                data_sample_cnt = 0;
                rx_current_state = RX_STATE_SEARCH;
                rx_phases_initialized = false;
                return true;
            }
            break;
        }
    }

    return false;
}
