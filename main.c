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
    //printf("=== ГЕНЕРАЦИЯ ПОЛНОГО ЭФИРНОГО ПАКЕТА С ТИШИНОЙ И ПРЕАМБУЛОЙ ===\n\n");
    printf("=== ГЕНЕРАЦИЯ ТЕСТОВОГО ПАКЕТА С ТИШИНОЙ, ПРЕАМБУЛОЙ И БЕЗ ДАННЫХ ВООБЩЕ (для теста коррелятора) ===\n\n");
    int pilot_samples=8000;

    viterbi_init_tables();
    dqpsk_modulator_t modulator;
    dqpsk_modulator_init(&modulator, 1000, 8000);

    qshort_channel_sim_t channel;
    channel_sim_init(&channel, 0.0, +0.0, 8000.0);

    // Генерируем рандомную длительность тишины (в отсчетах ЦАП при 8000 Гц)
    // 0.3..0.5 сек -> от 2400 до 4000 отсчетов
    int quiet_start_samples = 2400 + (rand() % (4000 - 2400 + 1));
    int quiet_end_samples   = 2400 + (rand() % (4000 - 2400 + 1));

    printf("[ПЛАН ПАКЕТА] Конец тишины: %d отсчетов (~%.3f с)\n", quiet_start_samples, (float)quiet_start_samples/8000.0f);
    printf("              Пилот-тон:    %d отсчетов (%5.3f с)\n", pilot_samples, pilot_samples/8000.0);
    printf("              Баркер-код:   8800 отсчетов (11 символов BPSK, 1.100 с)\n");
    printf("              (начало и конец):   %u и %u отсчетов\n", pilot_samples+quiet_start_samples,  pilot_samples+quiet_start_samples+8800);
    //printf("              Данные:       336000 отсчетов (420 символов, 42.000 с)\n");
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
        channel_sim_process(&channel, &clean_sample, &corrupted_sample); /* накладываем эмуляцию радиоканала */
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

    // --- ЭТАП 3: Преамбула Баркера (11 DBPSK-символов) ---
    // Используем классический код Баркера-11: 1, 1, 1, -1, -1, -1, 1, -1, -1, 1, -1
    // Классический код Баркера-11 (абсолютные знаки): 1, 1, 1, -1, -1, -1, 1, -1, -1, 1, -1
    int barker_signs[11] = {1, 1, 1, -1, -1, -1, 1, -1, -1, 1, -1};

    // Стартовая фаза преамбулы (пусть будет 0)
    unsigned short accumulated_tx_phase = 0;
    printf("[ПЕРЕДАТЧИК] Каноническая генерация DBPSK Баркер-11...\n");

    for (int sym = 0; sym < 11; sym++) {
        // Если +1 -> шаг +45 градусов (8192). Если -1 -> шаг -45 градусов (57344)
        unsigned short phase_shift = (barker_signs[sym] == 1) ? 8192 : 57344;

        for (int s = 0; s < 800; s++) {
            short raw_i, raw_q;
            // Сдвиг вносим СТРОГО в первом сэмпле нового символа
            unsigned short current_shift = (s == 0) ? phase_shift : 0;

            dqpsk_synth_tick(&modulator, current_shift, &raw_i, &raw_q);
            clean_sample.re = (float)raw_i / 32000.0f;
            clean_sample.im = (float)raw_q / 32000.0f;

            channel_sim_process(&channel, &clean_sample, &corrupted_sample);
            wav_write_sample(&wav_rx, &corrupted_sample);
        }
    }

    // --- ПОВТОРЕНИЕ ЭТАПА 2: Пилот-тон (Чистая несущая 1000 Гц без модуляции) ---
    // Чтобы не было фазового скачка, берем фазовую поправку = 0
    for (int i = 0; i < pilot_samples; i++) {
        short raw_i, raw_q;
        dqpsk_synth_tick(&modulator, 0, &raw_i, &raw_q);
        clean_sample.re = (float)raw_i / 32000.0f;
        clean_sample.im = (float)raw_q / 32000.0f;

        channel_sim_process(&channel, &clean_sample, &corrupted_sample);
        wav_write_sample(&wav_rx, &corrupted_sample);
    }

#if 0
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
#endif

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
    int16_t fft_incoming_i[256] = {0};
    int16_t fft_incoming_q[256] = {0};
    int sample_idx = 0;
    uint32_t sq_magnitudes[256] = {0};

    coarse_mixer_t mixer;
    fir_filter_t filter;
    fir_filter_init(&filter);

    costas_loop_t costas;
    costas_loop_init(&costas, modulator.sine_lut); // заимствуем таблицу синуса

    typedef enum {
        SEARCHING_PILOT,
        EXTRACTING_CANDY,
		EXTRACTING_DEBUG_BARKER
    } test_mode_t;

    test_mode_t mode = EXTRACTING_DEBUG_BARKER;//SEARCHING_PILOT;
    float detected_freq_error = 0.0f;
	FILE *csv = fopen ("spectrum.csv", "wt");
	FILE *barker_csv = fopen ("barker.csv", "wt");
	//fprintf (csv, "Sample,Correlation,NoiseFloor\n");
    fft_init_tables();
    int got_barker=0;

    // Переменные состояния детектора (вынести в rx_state структуры)
    int barker_lock_triggered = 0;
    int window_counter = 0;
    float ultimate_max_power = 0.0f;
    int ultimate_max_step = 0;

    while (wav_read_sample(&wav_in, &rx_sample) == 1) {
    	/*
    	1) check_spectrum_for_pilot
    	2) coarse_mixer_process
    	3) fir_filter_process
    	*/
    	if (mode == EXTRACTING_DEBUG_BARKER) {
            float barker_corr_power = 0.0f;
            // В момент перехода из БПФ в поиск Баркера принудительно обнуляем счетчики,
			// чтобы сетка 800 сэмплов коррелятора идеально совпала с физическим началом Баркера!
            /*if (b_step == (pilot_samples + quiet_start_samples)) {
				printf ("Сэмпл %u, взводим Баркера\n", b_step);
				barker_slider.sample_cnt = 0;
				barker_slider.delay_idx = 0;
				memset(&barker_slider.running_sum, 0, sizeof(barker_slider.running_sum));
			}*/

            barker_sliding_tick(&barker_slider, &rx_sample, &barker_corr_power, b_step);

            if (!barker_lock_triggered) {
                // Боевой порог 0.84 гарантированно выше бокового лепестка (0.825)!
                if (barker_corr_power >= 0.84f) {
                    barker_lock_triggered = 1;
                    window_counter = 0; // Запускаем таймер окна на 1 символ вперед
                    ultimate_max_power = barker_corr_power;
                    ultimate_max_step = b_step;
                }
            } else {
                // Мы внутри защищенного строб-окна главного купола (длительность 1 символ)
                if (barker_corr_power > ultimate_max_power) {
                    ultimate_max_power = barker_corr_power;
                    ultimate_max_step = b_step; // Непрерывно обновляем абсолютную вершину
                }

                window_counter++;
                if (window_counter >= 800) {
                    // Окно закрылось! Мы гарантированно поймали вершину и переждали все провалы
                    printf("🎯 🎯 🎯 [BARKER SYNC LOCK!] Временная сетка кадра успешно зафиксирована!\n");
                    printf("  -> Истинный пик кадра на отсчете: %d\n", ultimate_max_step);
                    printf("  -> Максимальная мощность: %.4f\n\n", ultimate_max_power);

                    barker_lock_triggered = 0; // Сброс триггера для следующего пакета

                    // Переходим в режим приема данных. Физический конец Баркера на 20276.
                    // Зная точный b_step вершины (например, 20002), выставляем строгую
                    // границу начала первого информационного символа:
                    int data_start_step = ultimate_max_step + (20276 - 20000);
                    printf ("Модельный data_start_step (внимание, костыль!!!): %u\n", data_start_step);
                    got_barker = 1;
                    break;
                }
            }

            wav_write_sample(&wav_out, &rx_sample);
            if ((b_step%50 == 0)) { fprintf(barker_csv, "%d, %.2f\n", b_step, barker_corr_power); }
    	} else if (mode == SEARCHING_PILOT) {
			// Переводим float в честный Q14 (масштаб 16384.0f), чтобы компенсировать внутренний сдвиг БПФ
	    	fft_incoming_i[sample_idx] = (int16_t)(rx_sample.re * 8192);//16384.0f);
	    	fft_incoming_q[sample_idx] = (int16_t)(rx_sample.im * 8192);//16384.0f);
	    	sample_idx++;

	    	// Запускаем БПФ раз в 128 отсчетов (чтобы сэкономить такты)
			// Каждые 128 отсчетов формируем строго последовательное окно для БПФ
			if ((sample_idx == 128) || (sample_idx == 256)) {
			    // Временный линейный буфер на стеке для БПФ (256 элементов)
			    int16_t fft_ready_i[256];
			    int16_t fft_ready_q[256];

			    // Формируем окно из последних 256 сэмплов без разрушения кольцевого буфера
			    // (Этот memcpy выполняется раз в 128 сэмплов, что почти бесплатно)
			    if (sample_idx == 128) {
			        // Окно состоит из: старый хвост [128..255] + свежая голова [0..127]
			        memcpy(&fft_ready_i[0],   &fft_incoming_i[128], 128 * sizeof(int16_t));
			        memcpy(&fft_ready_i[128], &fft_incoming_i[0],   128 * sizeof(int16_t));

			        memcpy(&fft_ready_q[0],   &fft_incoming_q[128], 128 * sizeof(int16_t));
			        memcpy(&fft_ready_q[128], &fft_incoming_q[0],   128 * sizeof(int16_t));
			    } else { // sample_idx == 256
			        // Окно состоит из: [0..127] + [128..255] (просто копируем подряд)
			        memcpy(fft_ready_i, fft_incoming_i, 256 * sizeof(int16_t));
			        memcpy(fft_ready_q, fft_incoming_q, 256 * sizeof(int16_t));
			        sample_idx = 0; // Сброс кольцевого индекса
			    }
			    // Запускаем БПФ на подготовленном линейном окне
			    fft_light_fixed256(fft_ready_i, fft_ready_q, sq_magnitudes);

			    float err = check_mcu_spectrum_for_pilot(sq_magnitudes);

				if (err != -999.0f) {
					detected_freq_error = err;
					printf("🎯 [БПФ ЛОК!] Несущая найдена! Ошибка частоты: %.2f Гц на отсчете %d\n",
						   detected_freq_error, b_step);

					// Инициализируем микшер на точную компенсацию частоты
					coarse_mixer_init(&mixer, detected_freq_error, modulator.sine_lut);
					mode = EXTRACTING_CANDY;
					//mode = EXTRACTING_DEBUG;

					FILE *f_spec = fopen("fft_snapshot.csv", "wt");
					fprintf(f_spec, "# Бин, Квадрат_Амплитуды, Частота_Гц\n");
					for (int i = 0; i < 128; i++) {
						fprintf(f_spec, "%d, %u, %.2f\n", i, sq_magnitudes[i], i * 31.25f);
					}
					fclose(f_spec);
					printf("💾 [DIAGNOSTIC] Срез спектра на отсчете 6000 сохранен в fft_snapshot.txt!\n");
				}
			}
			// Пока ищем — пишем в выходной файл тишину (нули), чтобы увидеть чистый старт
			//cplx_f32 silence = {0.0f, 0.0f};
			wav_write_sample(&wav_out, &rx_sample);//&silence);
		} else if (mode == EXTRACTING_CANDY) {
			static int costas_timer = 0; // костыль!
            cplx_f32 mixed_sample;
            cplx_f32 filtered_sample;
            cplx_f32 clean_candy_sample;
            cplx_f32 costas_locked_sample;

            // 1. Грубо докручиваем сигнал строго к ПЧ 1000 Гц
            coarse_mixer_process(&mixer, &rx_sample, &mixed_sample);
            wav_write_sample(&wav_out, &mixed_sample);

#if 0
            costas_loop_tick(&costas, &mixed_sample, &costas_locked_sample);

            costas_timer++;
            // Через 800 отсчетов (1 символ пилота) частота гарантированно захвачена
            if (costas_timer == 800) {
            	costas_loop_gear_shift(&costas);
                printf("🔒 [COSTAS FREQ LOCK] Частота зафиксирована на отсчете %d. Интегратор заторможен на значении: %.6f\n",
                       b_step, costas.freq_integrator);
            }
#endif

#if 0
            float barker_corr_power = 0.0f;
            if (barker_sliding_tick(&barker_slider, &costas_locked_sample, &barker_corr_power) == 1) {
                printf("🎯 🎯 🎯 [BARKER MATCH!] Идеальная временная засечка кадра найдена на отсчете %d! Мощность пика = %.4f\n",
                       b_step, barker_corr_power);
                // Фиксируем marker_index = b_step.
                // Отсчет информационных DQPSK-символов данных начнется строго отсюда!
                // mode = RECEIVING_DATA_PACKET;
            }
#endif

            // Пишем в CSV мощность Баркера для финальной визуализации
            //if ((b_step%50 == 0)&&b_step < 12000) fprintf(csv, "%d, %.4f\n", b_step, barker_corr_power);
            // Сбрасываем результат в WAV
            //wav_write_sample(&wav_out, &costas_locked_sample); //&clean_candy_sample);
             //&clean_candy_sample);
		}
		b_step++;
	}
    printf ("Тест RX-конвейера завершён, сигнатура Баркера %s\n", (got_barker == 1)?"НАЙДЕНА!" : "не обнаружена. Провал!");
	fclose(csv);
	fclose(barker_csv);
    wav_close(&wav_in);
    wav_close(&wav_out);

    return 0;
}
