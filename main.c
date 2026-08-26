#include "config.h"
#include "tx.h"
#include "rx.h"
#include "wav_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main(void) {
    printf("=== ЛАБОРАТОРНЫЙ СТЕНД: АДАПТИВНАЯ ПРЕАМБУЛА НА РАБОЧИХ ЧАСТОТАХ ===\n");

    tx_init();
    rx_init();

    FILE *wav_out = wav_open_write("preamble_test.wav", FS);

    // Искусственно задаем КВ-дрейф для проверки калибровки
    float simulated_kv_drift = -22.40f;
    uint32_t global_sample_idx = 0;
    int16_t iq_frame[2];

    // 1. ГЕНЕРАЦИЯ: Симулируем 300 сэмплов начального КВ шума (без полезного сигнала)
    for(int i=0; i<300; i++) {
        complex_f noise_sample = { 0, 0 };
        // Чистый шум малой амплитуды
        noise_sample.re = (((float)rand() / RAND_MAX) - 0.5f) * 0.02f;
        noise_sample.im = (((float)rand() / RAND_MAX) - 0.5f) * 0.02f;
        iq_frame[0] = (int16_t)(noise_sample.re * 16384.0f);
        iq_frame[1] = (int16_t)(noise_sample.im * 16384.0f);
        fwrite(iq_frame, sizeof(int16_t), 2, wav_out);

        rx_process_sample(&noise_sample, NULL);
        rx_process_sample(&noise_sample, NULL);
        global_sample_idx++;
    }

    // 2. ГЕНЕРАЦИЯ: Отправляем 10 символов преамбулы 0xA5
    for (int smb = 0; smb < PREAMBLE_SYMBOLS; smb++) {
        // Качаем фазы DDS рабочих частот
        tx_step_phase(PREAMBLE_PATTERN);

        for (int i = 0; i < TOTAL_SYMBOL_SAMPLES; i++) {
            complex_f sample;
            tx_get_sample(&sample);

            // Накладываем КВ-дрейф частоты
            float t = (float)global_sample_idx / FS;
            float phase = 2.0f * PI_F * simulated_kv_drift * t;
            complex_f distorted_sample;
            distorted_sample.re = sample.re * cosf(phase) - sample.im * sinf(phase);
            distorted_sample.im = sample.re * sinf(phase) + sample.im * cosf(phase);

            iq_frame[0] = (int16_t)(distorted_sample.re * 16384.0f);
            iq_frame[1] = (int16_t)(distorted_sample.im * 16384.0f);
            fwrite(iq_frame, sizeof(int16_t), 2, wav_out);

            // Скармливаем приёмнику по одному сэмплу
            rx_process_sample(&distorted_sample, NULL);
            global_sample_idx++;
        }
    }

    wav_close_write(wav_out);
    return 0;
}
