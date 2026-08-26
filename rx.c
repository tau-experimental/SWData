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

void rx_process_sample(complex_f *sample, uint8_t *result_nibble) {
    uint32_t current_tick = rx_global_timer;
    rx_global_timer++;

    int head = current_tick % TOTAL_SYMBOL_SAMPLES;
    data_buffer[head] = *sample;

    if (current_tick < (SEARCH_WIN_LEN * 2)) return;

    complex_f local_win_A[SEARCH_WIN_LEN];
    complex_f local_win_B[SEARCH_WIN_LEN];

    for (int i = 0; i < SEARCH_WIN_LEN; i++) {
        int idx_A = (head - SEARCH_WIN_LEN + i + TOTAL_SYMBOL_SAMPLES) % TOTAL_SYMBOL_SAMPLES;
        int idx_B = (head - (SEARCH_WIN_LEN * 2) + i + TOTAL_SYMBOL_SAMPLES) % TOTAL_SYMBOL_SAMPLES;
        local_win_A[i] = data_buffer[idx_A];
        local_win_B[i] = data_buffer[idx_B];
    }

    uint32_t time_A_start = current_tick - SEARCH_WIN_LEN;
    uint32_t time_B_start = current_tick - (SEARCH_WIN_LEN * 2);

    complex_f dft_win_A[NUM_DATA_TONES];
    complex_f dft_win_B[NUM_DATA_TONES];

    calc_sliding_dft(local_win_A, SEARCH_WIN_LEN, time_A_start, dft_win_A);
    calc_sliding_dft(local_win_B, SEARCH_WIN_LEN, time_B_start, dft_win_B);

    float signal_detection_index = 0.0f;
    float tone_jumps[NUM_DATA_TONES] = {0};

    for (int t = 0; t < NUM_DATA_TONES; t++) {
        float phi_A = atan2f(dft_win_A[t].im, dft_win_A[t].re);
        float phi_B = atan2f(dft_win_B[t].im, dft_win_B[t].re);

        float d_phi = phi_A - phi_B;
        if (d_phi > PI_F)  d_phi -= 2.0f * PI_F;
        if (d_phi < -PI_F) d_phi += 2.0f * PI_F;

        static float reference_d_phi;
        if (t == 0) {
            reference_d_phi = d_phi;
            tone_jumps[t] = d_phi;
        } else {
            float true_jump = d_phi - reference_d_phi;
            if (true_jump > PI_F)  true_jump -= 2.0f * PI_F;
            if (true_jump < -PI_F) true_jump += 2.0f * PI_F;

            tone_jumps[t] = true_jump;
            signal_detection_index += fabsf(true_jump);
        }
    }

    static uint32_t last_click_tick = 0;

    // Уменьшаем Hold-защиту до 32 сэмплов, чтобы увидеть МАКСИМУМ структуры сигнала
    if (signal_detection_index > 2.0f && (current_tick - last_click_tick) > 32) {
        uint32_t exact_click_sample = current_tick - SEARCH_WIN_LEN;
        uint32_t delta_from_last = exact_click_sample - last_click_tick;

        printf("[ОСЦИЛЛОГРАФ] Сэмпл: #%u | Шаг: %u | Метрика: %.2f | Сдвиги фаз тонов: [1000]=%.2f, [1200]=%.2f, [1400]=%.2f, [1600]=%.2f\n",
               exact_click_sample,
               (last_click_tick == 0) ? 0 : delta_from_last,
               signal_detection_index,
               tone_jumps[0] * 180.0f / PI_F,  // Переводим в градусы для наглядности
               tone_jumps[1] * 180.0f / PI_F,
               tone_jumps[2] * 180.0f / PI_F,
               tone_jumps[3] * 180.0f / PI_F);

        last_click_tick = exact_click_sample;
    }
}

