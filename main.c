#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "dsp_utils.h"
#include "modulator.h"
#include "complex_filter.h"
#include "demodulator.h"
#include "decoder.h"
#include "sync.h"
#include "wav_io.h"

#define FS 8000.0f
#define SYMBOL_LEN 800
#define RANDOM_SILENCE_LEN 650
#define TOTAL_SYMBOLS 100
#define TOTAL_SAMPLES (RANDOM_SILENCE_LEN + SYMBOL_LEN * TOTAL_SYMBOLS)+1000

int main(void) {
	wav_writer_t WaveDump;
    dsp_dds_init_table();

    wav_writer_open(&WaveDump, "SyncNoise.wav", 8000);

    modulator_t modulator;
    dsp_modulator_init(&modulator, FS, SYMBOL_LEN);

    complex_filter_bank_t filter_bank;
    dsp_complex_bank_init(&filter_bank, FS, 100.0f);

    demodulator_t demodulator;
    dsp_demodulator_init(&demodulator, FS, SYMBOL_LEN);

    clock_recovery_t sync;
    dsp_sync_init(&sync, SYMBOL_LEN);

    dsp_ema_filter_t sum_mag_filter;
    dsp_ema_init(&sum_mag_filter, 0.10f); // 10% нового сигнала, 90% истории

    dsp_decoder_t decoder;
    dsp_decoder_init(&decoder);

    //FILE *f_csv = fopen("sync_test.csv", "w");
    //fprintf(f_csv, "Sample,SumMag,StrobeMarker,DiffPhase1300\n");

    // Файл для сигнального созвездия
    //FILE *f_const = fopen("constellation.csv", "w");
    //fprintf(f_const, "Tone1300_I,Tone1300_Q,Tone1400_I,Tone1400_Q,Tone1500_I,Tone1500_Q,Tone1600_I,Tone1600_Q\n");

    // Генерируем массив случайных нибблов заранее
    //uint8_t tx_stream[TOTAL_SYMBOLS];
    //tx_stream[0] = 0x0; tx_stream[1] = 0x0; tx_stream[2] = 0x0; // Преамбула тишины
    //for(int i = 3; i < TOTAL_SYMBOLS; i++) { tx_stream[i] = rand() % 16; }; // Случайный ниббл от 0x0 до 0xF
    int current_symbol_idx = 0;
    // Тестовая последовательность для байта 0x73, затем 0xA5
    uint8_t tx_stream[] = {0x0, 0x0, 0x0, 0x0A, 0x7, 0x3, 0xA, 0x5, 0xB, 0xE, 0x0, 0x0};
    //uint8_t tx_stream[] = {0x0, 0x0, 0x0, 0x0A, 0x7, 0x3, 0xD, 0x5, 0xB, 0xE, 0x0, 0x0};
    uint32_t total_symbols = 12;
	uint32_t total_samples = RANDOM_SILENCE_LEN + SYMBOL_LEN * total_symbols + 2000;

    printf("Сбор данных для сигнального созвездия под шумом...\n");

    for (int n = 0; n < total_samples; n++) { // был макрос TOTAL_SAMPLES
        complex_f tx_sample = {0.0f, 0.0f};

        // Симулируем стартовую и завершающую тишину
        if ((n >= RANDOM_SILENCE_LEN) && (current_symbol_idx < sizeof(tx_stream))) {
        	current_symbol_idx = (n - RANDOM_SILENCE_LEN) / SYMBOL_LEN;
            uint8_t current_nibble = tx_stream[current_symbol_idx];

            /* === КВ-ДРЕЙФ ЧАСТОТЫ ПЕРЕДАТЧИКА === */
            // Включаем постоянный дрейф +2.0 Гц строго перед шагом модулятора
            float test_freqs[NUM_TONES] = {1300.0f, 1400.0f, 1500.0f, 1600.0f};
            //float current_drift = 2.0f; // +2 Гц фиксированной расстройки
            float current_drift = 3.0f * sinf(2.0f * M_PI_F * n / 3000.0f); // качающийся дрифт от -3.0 Гц до +3.0 Гц
            //float current_drift = 0.0f;
            for(int i = 0; i < NUM_TONES; i++) {
                dsp_dds_set_frequency(&modulator.tone_gen[i], test_freqs[i] + current_drift, FS);
            }
            /* =================================== */

            dsp_modulator_step(&modulator, current_nibble, &tx_sample);
        }

        /* === ВДАРИЛИ МОЩНЫМ ШУМОМ === */
        // Шум присутствует ВСЕГДА, даже в стартовой тишине, как в реальном радиоэфире!
        float noise_amplitude = 0.02f;
        float signal_scale = 0.98f;

        tx_sample.re = tx_sample.re * signal_scale + generate_white_noise() * noise_amplitude;
        tx_sample.im = tx_sample.im * signal_scale + generate_white_noise() * noise_amplitude;

        wav_writer_write_sample(&WaveDump, tx_sample.re, tx_sample.im);

        // Приемный тракт
        complex_f rx_filtered[NUM_TONES];
        dsp_complex_bank_process(&filter_bank, tx_sample, rx_filtered);

        complex_f demod_outputs[NUM_TONES];
        dsp_complex_bank_process(&filter_bank, tx_sample, rx_filtered); // Дубликат убираем, вызываем демод:
        dsp_demodulator_step(&demodulator, rx_filtered, demod_outputs);

        complex_f diff_outputs[NUM_TONES];
        dsp_demodulator_get_diff(&demodulator, demod_outputs, diff_outputs);

        // Считаем модули выходов корреляторов для синхронизатора
        float mags[NUM_TONES];
        float raw_sum_mag = 0.0f;
        for(int i = 0; i < NUM_TONES; i++) {
            mags[i] = sqrtf(c_mag2(demod_outputs[i]));
            raw_sum_mag += mags[i];
        }

        /* === НОВЫЙ КАСКАД КОНВЕЙЕРА: СГЛАЖИВАНИЕ === */
        float smoothed_sum_mag = dsp_ema_process(&sum_mag_filter, raw_sum_mag);


        // Шаг автомата синхронизации
        //float sum_mag = 0.0f;
        bool strobe = dsp_sync_step(&sync, smoothed_sum_mag);

        // Фаза для контроля
        float dp1300 = atan2f(diff_outputs[0].im, diff_outputs[0].re) * 180.0f / M_PI_F;

        if (strobe) {
#if 0
            int byte_ready = dsp_decoder_process_strobe(&decoder, diff_outputs);

            // Записываем в лог принятый ниббл
            if (n > (RANDOM_SILENCE_LEN + SYMBOL_LEN * 2)) {
                printf("[DECODER] Строб на сэмпле %d. Распознан ниббл: 0x%X\n", n, decoder.current_nibble);

                if (byte_ready) {
                    printf("[DECODER] >>> ПРИНЯТ ПОЛНЫЙ БАЙТ: 0x%02X <<<\n", decoder.received_byte);
                }
            }
#else
            /* === ВРЕМЕННЫЙ ЖЕСТКИЙ СИНХРОМАРКЕР ДЛЯ ТЕСТА ГЕОМЕТРИИ ===
               Передатчик излучает:
               Символ 0 (сэмплы 650-1450): 0x0
               Символ 1 (сэмплы 1450-2250): 0x0
               Символ 2 (сэмплы 2250-3050): 0x0
               Символ 3 (сэмплы 3050-3850): 0x0A (Наш маркер кадра!)
               Значит, строб на отметке ~3850 зафиксирует конец маркера 0x0A.
               Здесь мы жестко заставляем триггер встать в 0, чтобы СЛЕДУЮЩИЙ
               символ (0x7) гарантированно считался как верхний ниббл! */
            if (n >= 3800 && n <= 3900) {
                decoder.nibble_toggle = 0;
                printf("[DEBUG] Сетка нибблов жестко выровнена на сэмпле %d\n", n);
            }
            /* ========================================================== */

            int byte_ready = dsp_decoder_process_strobe(&decoder, diff_outputs);

            // Выводим логи только для полезных данных (после маркера)
            if (n > 3900) {
                printf("[DECODER] Строб на сэмпле %d. Распознан ниббл: 0x%X\n", n, decoder.current_nibble);

                if (byte_ready) {
                    printf("[DECODER] >>> ПРИНЯТ ПОЛНЫЙ БАЙТ: 0x%02X <<<\n", decoder.received_byte);
                }
            }
#endif
        }
    }

    //fclose(f_csv);
    //fclose(f_const);
    wav_writer_close(&WaveDump);
    //printf("Тест завершен. Результаты в 'sync_test.csv'\n");
    return 0;
}
