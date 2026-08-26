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

static uint32_t last_click_tick = 0;

void rx_init(void) {
    rx_current_state = RX_STATE_SEARCH;
    rx_global_timer = 0;
    sync_absolute_tick = 0;
    freq_offset_hz = 0.0f;
    preamble_symbol_cnt = 0;
    for(int i=0; i<TOTAL_SYMBOL_SAMPLES; i++) { data_buffer[i].re = 0.0f; data_buffer[i].im = 0.0f; }
    for(int i=0; i<NUM_DATA_TONES; i++) { prev_tone_integrals[i].re = 0.0f; prev_tone_integrals[i].im = 0.0f; }
    printf("[Приемник] Режим SEARCH: Интегральный N-сэмпловый дискриминатор скачков запущен.\n");
}

void calc_sliding_dft(complex_f *buf_ptr, int w_len, int timer_base, complex_f *results) {
    for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
        float tone_freq = data_tones[tone]; // ПРИЁМНИК ВСЕГДА ИЩЕТ НА ИДЕАЛЬНЫХ ЧАСТОТАХ (1000, 1200...)
        results[tone].re = 0.0f;
        results[tone].im = 0.0f;

        for (int i = 0; i < w_len; i++) {
            // Вычисляем абсолютный индекс времени для текущего сэмпла
            uint32_t absolute_sample_tick = timer_base + i;

            // Вместо деления огромного времени на FS, считаем набег фазы на один сэмпл:
            float phase_per_sample = 2.0f * PI_F * tone_freq / FS;

            // Полный угол равен phase_per_sample * absolute_sample_tick.
            // Сворачиваем его по модулю 2*PI, используя fmodf, чтобы защитить точность float!
            float angle = fmodf((float)absolute_sample_tick * phase_per_sample, 2.0f * PI_F);

            float cos_ref = cosf(angle);
            float sin_ref = sinf(angle);

            // Квалифицированное комплексное ДПФ-проектирование
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

    if (rx_global_timer > (SEARCH_WIN_LEN * 2)) {
        complex_f dft_win_A[NUM_DATA_TONES];
        complex_f dft_win_B[NUM_DATA_TONES];

        int offset_A = TOTAL_SYMBOL_SAMPLES - SEARCH_WIN_LEN;
        int offset_B = TOTAL_SYMBOL_SAMPLES - (SEARCH_WIN_LEN * 2);

        uint32_t time_A_start = rx_global_timer - SEARCH_WIN_LEN + 1;
        uint32_t time_B_start = rx_global_timer - (SEARCH_WIN_LEN * 2) + 1;

        // Считаем ДПФ с честной fmodf-тригонометрией
        calc_sliding_dft(&data_buffer[offset_A], SEARCH_WIN_LEN, time_A_start, dft_win_A);
        calc_sliding_dft(&data_buffer[offset_B], SEARCH_WIN_LEN, time_B_start, dft_win_B);

        float signal_detection_index = 0.0f;

        for (int t = 0; t < NUM_DATA_TONES; t++) {
            float mag_A_sq = dft_win_A[t].re * dft_win_A[t].re + dft_win_A[t].im * dft_win_A[t].im;
            float mag_B_sq = dft_win_B[t].re * dft_win_B[t].re + dft_win_B[t].im * dft_win_B[t].im;

            if (mag_A_sq > 0.001f && mag_B_sq > 0.001f) {
                float dot_re = dft_win_A[t].re * dft_win_B[t].re + dft_win_A[t].im * dft_win_B[t].im;
                float cos_diff = dot_re / sqrtf(mag_A_sq * mag_B_sq);

                signal_detection_index += (1.0f - fabsf(cos_diff));
            }
        }

        // Если метрика подскочила, и мы еще не рапортовали об этом клике в данном окне
        // Порог 2.5f–2.6f оптимален для коротких окон 16 сэмплов
        if (signal_detection_index > 2.5f && (rx_global_timer - last_click_tick) > 32) {
            uint32_t exact_click_sample = rx_global_timer - SEARCH_WIN_LEN;
            uint32_t delta_from_last = exact_click_sample - last_click_tick;

            printf("[КЛИК ФАЗЫ] Абс. сэмпл: #%u | Дистанция от прошлого: %u сэмплов | Метрика: %.2f\n",
                   exact_click_sample,
                   (last_click_tick == 0) ? 0 : delta_from_last,
                   signal_detection_index);

            last_click_tick = exact_click_sample;
        }
    }
}
