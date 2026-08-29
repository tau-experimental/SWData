#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "conv_encoder.h"
#include "puncturing.h"
#include "modulator.h"
#include "channel_sim.h"
#include "fft_sync.h"
#include "wav_io.h"
#include "carrier_recovery.h"
#include "barker_sync.h"

int main(void) {
    srand((unsigned int)time(NULL));
    printf("=== ГЕНЕРАЦИЯ ПОЛНОГО ЭФИРНОГО ПАКЕТА С ТИШИНОЙ И ПРЕАМБУЛОЙ ===\n\n");

    viterbi_init_tables();
    dqpsk_modulator_t modulator;
    dqpsk_modulator_init(&modulator, 1000, 8000);

    qshort_channel_sim_t channel;
    // SNR = 6 дБ, Дрейф = +20 Гц
    channel_sim_init(&channel, 6.0, 20.0, 8000.0);

    // Генерируем рандомную длительность тишины (в отсчетах ЦАП при 8000 Гц)
    // 0.3..0.5 сек -> от 2400 до 4000 отсчетов
    int quiet_start_samples = 2400 + (rand() % (4000 - 2400 + 1));
    int quiet_end_samples   = 2400 + (rand() % (4000 - 2400 + 1));

    printf("[ПЛАН ПАКЕТА] Старт тишины: %d отсчетов (~%.3f с)\n", quiet_start_samples, (float)quiet_start_samples/8000.0f);
    printf("              Пилот-тон:    8000 отсчетов (1.000 с)\n");
    printf("              Баркер-код:   4800 отсчетов (6 символов, 0.600 с)\n");
    printf("              Данные:       336000 отсчетов (420 символов, 42.000 с)\n");
    printf("              Конец тишины: %d отсчетов (~%.3f с)\n\n", quiet_end_samples, (float)quiet_end_samples/8000.0f);

    // Подготовка случайных инфо-данных пакета
    static unsigned char tx_payload_bits[840];
    static unsigned char tx_encoded_1_2[1680];
    static unsigned char tx_punctured_5_6[1008];
    for (int i = 0; i < 834; i++) tx_payload_bits[i] = rand() % 2;
    for (int i = 834; i < 840; i++) tx_payload_bits[i] = 0; // Zero-Tail

    conv_encoder_t encoder;
    conv_encoder_reset(&encoder);
    conv_encode_pure_1_2(&encoder, tx_payload_bits, tx_encoded_1_2);
    apply_puncturing(tx_encoded_1_2, tx_punctured_5_6);

    wav_stream_t wav_rx;
    if (wav_open_write(&wav_rx, "rx_packet_full.wav") != 1) return -1;

    cplx_f32 clean_sample, corrupted_sample;

    // --- ЭТАП 1: Начальная тишина (чистый шум эфира) ---
    clean_sample.re = 0.0f;
    clean_sample.im = 0.0f;
    for (int i = 0; i < quiet_start_samples; i++) {
        channel_sim_process(&channel, &clean_sample, &corrupted_sample);
        wav_write_sample(&wav_rx, &corrupted_sample);
    }

    // --- ЭТАП 2: Пилот-тон 1.0 сек (Чистая несущая 1000 Гц без модуляции) ---
    // Чтобы не было фазового скачка, берем фазовую поправку = 0
    for (int i = 0; i < 8000; i++) {
        short raw_i, raw_q;
        dqpsk_synth_tick(&modulator, 0, &raw_i, &raw_q);
        clean_sample.re = (float)raw_i / 32000.0f;
        clean_sample.im = (float)raw_q / 32000.0f;

        channel_sim_process(&channel, &clean_sample, &corrupted_sample);
        wav_write_sample(&wav_rx, &corrupted_sample);
    }

    // --- ЭТАП 3: Преамбула Баркера (6 DQPSK-символов) ---
    // Модулятор сам прогонит биты Баркера через dqpsk_synth_tick и выдаст в канал
    // Для совместимости с потоковым зашумлением временно перенаправим вывод.
    // Но проще сделать это прямо в цикле здесь, чтобы сохранить сквозной канал:
    unsigned char barker_bits[12] = {1,1, 1,0, 0,0, 1,0, 0,1, 0,0};
    for (int sym = 0; sym < 6; sym++) {
        unsigned short phase_shift = dqpsk_get_phase_shift(barker_bits[sym * 2], barker_bits[sym * 2 + 1]);
        for (int s = 0; s < 800; s++) {
            short raw_i, raw_q;
            unsigned short current_shift = (s == 0) ? phase_shift : 0;
            dqpsk_synth_tick(&modulator, current_shift, &raw_i, &raw_q);
            clean_sample.re = (float)raw_i / 32000.0f;
            clean_sample.im = (float)raw_q / 32000.0f;

            channel_sim_process(&channel, &clean_sample, &corrupted_sample);
            wav_write_sample(&wav_rx, &corrupted_sample);
        }
    }

    // --- ЭТАП 4: Информационные данные (420 символов) ---
    for (int sym = 0; sym < 420; sym++) {
        unsigned short phase_shift = dqpsk_get_phase_shift(tx_punctured_5_6[sym * 2], tx_punctured_5_6[sym * 2 + 1]);
        for (int s = 0; s < 800; s++) {
            short raw_i, raw_q;
            unsigned short current_shift = (s == 0) ? phase_shift : 0;
            dqpsk_synth_tick(&modulator, current_shift, &raw_i, &raw_q);
            clean_sample.re = (float)raw_i / 32000.0f;
            clean_sample.im = (float)raw_q / 32000.0f;

            channel_sim_process(&channel, &clean_sample, &corrupted_sample);
            wav_write_sample(&wav_rx, &corrupted_sample);
        }
    }

    // --- ЭТАП 5: Финальная тишина (чистый шум эфира) ---
    clean_sample.re = 0.0f;
    clean_sample.im = 0.0f;
    for (int i = 0; i < quiet_end_samples; i++) {
        channel_sim_process(&channel, &clean_sample, &corrupted_sample);
        wav_write_sample(&wav_rx, &corrupted_sample);
    }

    wav_close(&wav_rx);
    printf("🎉 СИГНАЛ ЗАПИСАН В 'rx_packet_full.wav'. Тракт передачи полностью готов к приему.\n");
    //--------------------------------- ПРИЁМ
    // Verification snippet to evaluate Pilot Tone detection under SNR = 6dB
#if 0
    printf("\n=== RUNNING EXTRACT FROM RECEIVER SPECTRAL ANALYSIS ===\n");
    fft_init_tables();

    wav_stream_t wav_in;
    wav_open_read(&wav_in, "rx_packet_full.wav");

    cplx_f32 block_float[1024];
    int16_t block_mcu_i[256];
    int16_t block_mcu_q[256];

    float heavy_spectrum[1024];
    uint32_t light_spectrum[256];

    // Read deep enough into the file where the Pilot Tone is guaranteed to be active
    // Skip 6000 samples (~0.75 seconds of mixed silence + early pilot)
    cplx_f32 temp;
    for (int s = 0; s < 6000; s++) wav_read_sample(&wav_in, &temp);

    // Read data arrays for both tests concurrently
    for (int i = 0; i < 1024; i++) {
        wav_read_sample(&wav_in, &block_float[i]);
        if (i < 256) {
            // Convert float IQ back to standard 16-bit signed integer values for the MCU block
            block_mcu_i[i] = (int16_t)(block_float[i].re * 32000.0f);
            block_mcu_q[i] = (int16_t)(block_float[i].im * 32000.0f);
        }
    }
    wav_close(&wav_in);

    // Compute both spectra
    fft_heavy_1024(block_float, heavy_spectrum);
    fft_light_fixed256(block_mcu_i, block_mcu_q, light_spectrum);

    // Print Heavy FFT results near 1000Hz (Bins 120 to 140 for N=1024, res = 7.8Hz)
    printf("\n[PC TRUTH MODEL - 1024 FLOAT FFT] Slices near 1000Hz:\n");
    for (int b = 126; b <= 134; b++) {
        printf("  Bin %d (~%.1f Hz): Magnitude = %.2f\n", b, b * 7.8125f, heavy_spectrum[b]);
    }

    // Print Light MCU FFT results near 1000Hz (Bins 30 to 35 for N=256, res = 31.25Hz)
    printf("\n[MCU PRODUCTION - 256 FIXED-POINT FFT] Slices near 1000Hz:\n");
    for (int b = 30; b <= 35; b++) {
        printf("  Bin %d (~%.1f Hz): Energy Value = %u\n", b, b * 31.25f, light_spectrum[b]);
    }
#else
    printf("\n=== СКВОЗНОЙ ТЕСТ СИНХРОНИЗАЦИИ: ФАПЧ + БАРКЕР ===\n");

	pll_tracker_t pll;
	pll_tracker_init(&pll, 33, modulator.sine_lut); // Инициализация бином 33

	barker_sync_t barker;
	barker_sync_init(&barker);

	wav_stream_t wav_in;
	wav_open_read(&wav_in, "rx_packet_full.wav");

	cplx_f32 rx_sample, pll_sample;
	float corr_power = 0.0f;
	int b_step = 0;

	while (wav_read_sample(&wav_in, &rx_sample) == 1) {
		// 1. Выравниваем частоту через ФАПЧ
		pll_tracker_tick(&pll, &rx_sample, &pll_sample);

		// 2. Скармливаем очищенный отсчет коррелятору Баркера
		if (barker_sync_tick(&barker, &pll_sample, &corr_power) == 1) {
			printf("\n🎯 [МАРКЕР ОБНАРУЖЕН!] Отсчет файла: %d | Мощность пика корреляции = %.4f\n",
				   b_step, corr_power);
			printf("⏰ Временная засечка зафиксирована. Начинается отсчет информационных бит пакета!\n\n");
		}

		// Выводим срез мощности каждые 800 отсчетов (в моменты вычисления символов преамбулы)
		if (b_step > 0 && b_step % 800 == 0 && corr_power > 0.001f) {
			printf("Символ на отсчете %5d: Текущая мощность корреляции Баркера = %.4f\n", b_step, corr_power);
		}

		b_step++;
	}
    wav_close(&wav_in);
#endif
    return 0;
}
