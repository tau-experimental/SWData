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
#include "rx.h"
#include "costas_loop.h"

int main(void) {
    srand((unsigned int)time(NULL));
    printf("=== ГЕНЕРАЦИЯ ПОЛНОГО ЭФИРНОГО ПАКЕТА С ТИШИНОЙ И ПРЕАМБУЛОЙ ===\n\n");
    int pilot_samples=8000;

    viterbi_init_tables();
    dqpsk_modulator_t modulator;
    dqpsk_modulator_init(&modulator, 1000, 8000);

    qshort_channel_sim_t channel;
    // SNR = 6 дБ, Дрейф = +40 Гц
    channel_sim_init(&channel, 16.0, 10, 8000.0);

    // Генерируем рандомную длительность тишины (в отсчетах ЦАП при 8000 Гц)
    // 0.3..0.5 сек -> от 2400 до 4000 отсчетов
    int quiet_start_samples = 2400 + (rand() % (4000 - 2400 + 1));
    int quiet_end_samples   = 2400 + (rand() % (4000 - 2400 + 1));

    printf("[ПЛАН ПАКЕТА] Конец тишины: %d отсчетов (~%.3f с)\n", quiet_start_samples, (float)quiet_start_samples/8000.0f);
    printf("              Пилот-тон:    %d отсчетов (%5.3f с)\n", pilot_samples, pilot_samples/8000.0);
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

    // --- ЭТАП 2: Пилот-тон (Чистая несущая 1000 Гц без модуляции) ---
    // Чтобы не было фазового скачка, берем фазовую поправку = 0
    for (int i = 0; i < pilot_samples; i++) {
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

    printf("\n=== ИЩЕМ, КРУТИМ, ФИЛЬТРУЕМ НЕСУЩУЮ: БПФ, CoarseMix, CostasLoop ===\n");

	pll_tracker_t pll;
	//pll_tracker_init(&pll, 33, modulator.sine_lut); // Инициализация бином 33 <- жульнический костыль априорного знания

	barker_sliding_t barker_slider;
	barker_sliding_init(&barker_slider);

	wav_stream_t wav_in, wav_out;
	wav_open_read(&wav_in, "rx_packet_full.wav");
	wav_open_write(&wav_out, "rx_packet_filtered.wav");

	cplx_f32 rx_sample;
	int b_step = 0;
    // Очередь (буфер) для накопления сэмплов перед БПФ
    int16_t fft_buf_i[256] = {0};
    int16_t fft_buf_q[256] = {0};
    uint32_t sq_magnitudes[256] = {0};

    coarse_mixer_t mixer;
    fir_filter_t filter;
    fir_filter_init(&filter);

    costas_loop_t costas;
    costas_loop_init(&costas, modulator.sine_lut); // заимствуем таблицу синуса

    typedef enum {
        SEARCHING_PILOT,
        EXTRACTING_CANDY
    } test_mode_t;

    test_mode_t mode = SEARCHING_PILOT;
    float detected_freq_error = 0.0f;
	FILE *csv = fopen ("barker.csv", "wt");
	//fprintf (csv, "Sample,Correlation,NoiseFloor\n");
    fft_init_tables();
    while (wav_read_sample(&wav_in, &rx_sample) == 1) {
    	/*
    	1) check_spectrum_for_pilot
    	2) coarse_mixer_process
    	3) fir_filter_process
    	*/
    	for (int i = 0; i < 255; i++) {
			fft_buf_i[i] = fft_buf_i[i + 1];
			fft_buf_q[i] = fft_buf_q[i + 1];
		}
		fft_buf_i[255] = (int16_t)(rx_sample.re * 8192.0f);
		fft_buf_q[255] = (int16_t)(rx_sample.im * 8192.0f);

	    if (b_step == 6000) {
	        FILE *f_spec = fopen("fft_snapshot.txt", "wt");
	        fft_light_fixed256(fft_buf_i, fft_buf_q, sq_magnitudes);

	        fprintf(f_spec, "# Бин, Квадрат_Амплитуды, Частота_Гц\n");
	        for (int i = 20; i < 50; i++) {
	            fprintf(f_spec, "%d, %u, %.2f\n", i, sq_magnitudes[i], i * 31.25f);
	        }
	        fclose(f_spec);
	        printf("💾 [DIAGNOSTIC] Срез спектра на отсчете 6000 сохранен в fft_snapshot.txt!\n");
	    }

		if (mode == SEARCHING_PILOT) {
			// Запускаем БПФ раз в 128 отсчетов (чтобы сэкономить такты)
			if (b_step > 256 && (b_step % 128) == 0) {
				fft_light_fixed256(fft_buf_i, fft_buf_q, sq_magnitudes);
				float err = check_mcu_spectrum_for_pilot(sq_magnitudes);
				if (0){
					int jj;
					printf("Running Spectrum: ");
					for (jj = 27; jj < 37; jj++) {
						printf ("%9.2f ", sqrt((double)sq_magnitudes[jj]));
					}
					printf("\n");
				}

				if (err != -999.0f) {
					detected_freq_error = err;
					printf("🎯 [БПФ ЛОК!] Несущая найдена! Ошибка частоты: %.2f Гц на отсчете %d\n",
						   detected_freq_error, b_step);

					// Инициализируем микшер на точную компенсацию частоты
					coarse_mixer_init(&mixer, detected_freq_error, modulator.sine_lut);
					mode = EXTRACTING_CANDY;
				}
			}

			// Пока ищем — пишем в выходной файл тишину (нули), чтобы увидеть чистый старт
			cplx_f32 silence = {0.0f, 0.0f};
			wav_write_sample(&wav_out, &silence);
		}
		else if (mode == EXTRACTING_CANDY) {
			static int costas_timer = 0; // костыль!
            cplx_f32 mixed_sample;
            cplx_f32 filtered_sample;
            cplx_f32 clean_candy_sample;
            cplx_f32 costas_locked_sample;

            // 1. Грубо докручиваем сигнал строго к ПЧ 1000 Гц
            coarse_mixer_process(&mixer, &rx_sample, &mixed_sample);

            // 2. Пропускаем через НОВЫЙ фильтр, который срежет НЧ-грязь
            // (Использует fir_coeffs_candy внутри fir_filter_process)
            //fir_filter_process(&filter, &mixed_sample, &filtered_sample);
            //fir_filter_complex_process(&filter, &mixed_sample, &filtered_sample);

            // 3. ВОЗВРАЩАЕМ ГРОМКОСТЬ: фиксированное усиление x3.5
            float ampl = .5f;
            clean_candy_sample.re = filtered_sample.re * ampl;
            clean_candy_sample.im = filtered_sample.im * ampl;

            // Защита от жесткого клиппинга float на выходе перед записью в WAV
            if (clean_candy_sample.re > 1.0f)  clean_candy_sample.re = 1.0f;
            if (clean_candy_sample.re < -1.0f) clean_candy_sample.re = -1.0f;
            if (clean_candy_sample.im > 1.0f)  clean_candy_sample.im = 1.0f;
            if (clean_candy_sample.im < -1.0f) clean_candy_sample.im = -1.0f;

            costas_loop_tick(&costas, &mixed_sample, &costas_locked_sample);

            costas_timer++;
            // Через 800 отсчетов (1 символ пилота) частота гарантированно захвачена
            if (costas_timer == 800) {
            	costas_loop_gear_shift(&costas);
                printf("🔒 [COSTAS FREQ LOCK] Частота зафиксирована на отсчете %d. Интегратор заторможен на значении: %.6f\n",
                       b_step, costas.freq_integrator);
            }
            float barker_corr_power = 0.0f;
            if (barker_sliding_tick(&barker_slider, &costas_locked_sample, &barker_corr_power) == 1) {
                printf("🎯 🎯 🎯 [BARKER MATCH!] Идеальная временная засечка кадра найдена на отсчете %d! Мощность пика = %.4f\n",
                       b_step, barker_corr_power);

                // Фиксируем marker_index = b_step.
                // Отсчет информационных DQPSK-символов данных начнется строго отсюда!
                // mode = RECEIVING_DATA_PACKET;
            }

            // Пишем в CSV мощность Баркера для финальной визуализации
            if ((b_step%50 == 0)&&b_step < 12000) fprintf(csv, "%d, %.4f\n", b_step, barker_corr_power);
            // Сбрасываем результат в WAV
            wav_write_sample(&wav_out, &costas_locked_sample); //&clean_candy_sample);
		}
		b_step++;
	}
	fclose(csv);
    wav_close(&wav_in);
    wav_close(&wav_out);

    return 0;
}
