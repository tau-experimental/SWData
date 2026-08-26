#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

#include "config.h"
#include "tx.h"
#include "rx.h"
#include "wav_io.h"

// Внешние функции конвейера из rx.c
extern void rx_pipeline_init(void);
extern void stage2_bpf_filter(int tone_idx, const complex_f *input);



// Генератор белого шума (Бокс-Мюллер)
static float get_noise(float sigma) {
    if (sigma <= 0.0f) return 0.0f;
    float u1 = (float)rand() / (float)RAND_MAX;
    float u2 = (float)rand() / (float)RAND_MAX;
    if (u1 < 1e-9f) u1 = 1e-9f;
    return sigma * sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI_F * u2);
}

void run_bpf_test_scenario(const char* name, float test_freq, float noise_level, const char* out_wave_pfx) {
    printf("\n--- Запуск подкаста: %s (Сигнал: %.1f Гц, Шум: %.2f) ---\n", name, test_freq, noise_level);

    rx_pipeline_init(); // Сброс всех фильтров перед тестом

    // Открываем WAV-файлы для записи результатов
    char fn_in[128], fn_f1[128], fn_f2[128];
    sprintf(fn_in, "%s_input.wav", out_wave_pfx);
    sprintf(fn_f1, "%s_output_bpf1000.wav", out_wave_pfx);
    sprintf(fn_f2, "%s_output_bpf1200.wav", out_wave_pfx);

    FILE* f_in = wav_open_write(fn_in, (uint32_t)FS);
    FILE* f_f1 = wav_open_write(fn_f1, (uint32_t)FS);
    FILE* f_f2 = wav_open_write(fn_f2, (uint32_t)FS);

    float tx_phase = 0.0f;
    const int test_len = 12000; // 1.5 секунды записи для Audacity
    printf ("Начало прогона: tx_phase: %+2.2f, test_freq = %4.1f, noise level = %2.2f\n", tx_phase, test_freq, noise_level);

    for (int n = 0; n < test_len; n++) {
        complex_f tx_signal = {0.0f, 0.0f};

        // Генерируем тестовый тон, если частота > 0
        if (test_freq > 0.0f) {
            tx_phase += 2.0f * PI_F * test_freq / FS;
            if (tx_phase > PI_F) tx_phase -= 2.0f * PI_F;
            tx_signal.re = cosf(tx_phase);
            tx_signal.im = sinf(tx_phase);
        }

        // Добавляем шум, если задан
        tx_signal.re += get_noise(noise_level);
        tx_signal.im += get_noise(noise_level);

        // Записываем то, что летит на вход фильтрам
        wav_write_sample(f_in, tx_signal.re, tx_signal.im);

        // Пропускаем СТАТИЧЕСКИЙ сигнал через независимые фильтры (БЕЗ АПЧ!)
        for (int t = 0; t < NUM_DATA_TONES; t++) {
            stage2_bpf_filter(t, &tx_signal);
        }

        // оцениваем частотный сдвиг
        stage3_frequency_assessment();

        // Записываем выходы фильтра 1000 Гц и фильтра 1200 Гц
        wav_write_sample(f_f1, rx_bpf_bank[0].y_curr.re, rx_bpf_bank[0].y_curr.im);
        wav_write_sample(f_f2, rx_bpf_bank[1].y_curr.re, rx_bpf_bank[1].y_curr.im);

        // Короткий лог в консоль для визуального контроля амплитуд
        if (n == 200 || (n % 1000)==0) {
            float amp0 = sqrtf(rx_bpf_bank[0].y_curr.re*rx_bpf_bank[0].y_curr.re + rx_bpf_bank[0].y_curr.im*rx_bpf_bank[0].y_curr.im);
            float amp1 = sqrtf(rx_bpf_bank[1].y_curr.re*rx_bpf_bank[1].y_curr.re + rx_bpf_bank[1].y_curr.im*rx_bpf_bank[1].y_curr.im);
            float amp2 = sqrtf(rx_bpf_bank[2].y_curr.re*rx_bpf_bank[2].y_curr.re + rx_bpf_bank[2].y_curr.im*rx_bpf_bank[2].y_curr.im);
            float amp3 = sqrtf(rx_bpf_bank[3].y_curr.re*rx_bpf_bank[3].y_curr.re + rx_bpf_bank[3].y_curr.im*rx_bpf_bank[3].y_curr.im);
            printf("  [Сэмпл %04d] Амплитуда на выходах BPF: (%6.3f, %6.3f, %6.3f, %6.3f)\n",
            		n, amp0, amp1, amp2, amp3);

            printf("  Оценка частотных сдвигов: (%+3.3f, %+3.3f, %+3.3f. %+3.3f)\n",
            		ch_cfo_estimates[0], ch_cfo_estimates[1], ch_cfo_estimates[2], ch_cfo_estimates[3]);
            printf("  smooth_cfo: %+4.2f Hz\n", debug_smooth_cfo_hz );
        }
    }

    wav_close(f_in);
    wav_close(f_f1);
    wav_close(f_f2);
    printf("  Экспорт в файлы завершен успешно.\n");
}

int main(void) {
	float shift = -32.0;
    // Сценарий А: Чистый тон 1000 Гц (Должен ожить только BPF-1000)
    run_bpf_test_scenario("Чистый тон 1000 Гц + сдвиг", 1000.0f + shift, 0.0f, "test_tone1000");

    // Сценарий Б: Чистый тон 1200 Гц (Должен ожить только BPF-1200)
    run_bpf_test_scenario("Чистый тон 1200 Гц + сдвиг", 1200.0f + shift, 0.0f, "test_tone1200");
    run_bpf_test_scenario("Чистый тон 1400 Гц + сдвиг", 1400.0f + shift, 0.0f, "test_tone1400");
    shift = 25.0;
    run_bpf_test_scenario("Чистый тон 1600 Гц + сдвиг", 1600.0f + shift, 0.0f, "test_tone1600");

    shift = 0.0;
    // Сценарий В: Экстремальный белый шум без полезного сигнала
    run_bpf_test_scenario("Чистый белый шум", 0.0f, 0.5f, "test_pure_noise");

    return 0;
}
