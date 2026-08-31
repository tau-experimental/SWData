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
#include "mls_sync.h"
#include "clk_detect.h"
#include "schmidl_cox.h"

int main(void) {
    srand((unsigned int)time(NULL));
    //printf("=== ГЕНЕРАЦИЯ ПОЛНОГО ЭФИРНОГО ПАКЕТА С ТИШИНОЙ И ПРЕАМБУЛОЙ ===\n\n");
    printf("=== ГЕНЕРАЦИЯ ТЕСТОВОГО ПАКЕТА С ТИШИНОЙ, ПРЕАМБУЛОЙ И БЕЗ ДАННЫХ ВООБЩЕ (для теста коррелятора) ===\n\n");
    int pilot_samples=8000;
    int mls31_start_samples = 31*800;
    int synchrodummy = 800;

    viterbi_init_tables();
    dqpsk_modulator_t modulator;
    dqpsk_modulator_init(&modulator, 1000, 8000);

    qshort_channel_sim_t channel;
    channel_sim_init(&channel, -3.0, 1.0, 8000.0);

    // Генерируем рандомную длительность тишины (в отсчетах ЦАП при 8000 Гц)
    // от 2000 до 6000 отсчетов
    int quiet_start_samples = 2000 + (rand() % (6000 - 2000 + 1));
    int quiet_end_samples   = 2400 + (rand() % (4000 - 2400 + 1));

    uint32_t total = 0;
    uint32_t preamble_end_sample;
    printf("[ПЛАН ПАКЕТА] Конец тишины: %d отсчетов (~%.3f с)\n", quiet_start_samples, (float)quiet_start_samples/8000.0f);
    total += quiet_start_samples;
    printf("              Пилот-тон:    %d отсчетов (%5.3f с)\n", pilot_samples, pilot_samples/8000.0);
    total += pilot_samples;

    printf("              Синхропустышка:    %d отсчетов (%5.3f с)\n", synchrodummy, synchrodummy/8000.0);
    total += synchrodummy;

    printf("              MLS-31:   %u отсчетов (31 символ BPSK, ~%.3f с)\n", mls31_start_samples, (float)mls31_start_samples/8000.0f);
    total += mls31_start_samples;
    preamble_end_sample = total;
    printf("              (начало и конец):   %u и %u отсчетов\n", pilot_samples+quiet_start_samples,  pilot_samples+quiet_start_samples+mls31_start_samples);
    //printf("              Данные:       336000 отсчетов (420 символов, 42.000 с)\n");
    //
    printf("              Пилот-тон:    %d отсчетов (%5.3f с)\n", pilot_samples, pilot_samples/8000.0);
    total += pilot_samples;
    printf("              Конец тишины: %d отсчетов (~%.3f с)\n", quiet_end_samples, (float)quiet_end_samples/8000.0f);
    total += quiet_end_samples;

    printf("              Всего: %u отсчетов (~%.3f с)\n\n", total, (float)total/8000.0f);

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
    // --- ЭТАП 3.1: генерация синхро-пустышки (просто один шаг фазы на +45)
    unsigned short current_absolute_synchodummy_phase = 57344; // фааза синхропустышки
    for (int s = 0; s < 800; s++) {
        short raw_i, raw_q;

        // Передаем ТЕКУЩУЮ СТАБИЛЬНУЮ ФАЗУ символа на протяжении всех 800 сэмплов!
        dqpsk_synth_tick(&modulator, current_absolute_synchodummy_phase, &raw_i, &raw_q);

        clean_sample.re = (float)raw_i / 32000.0f;
        clean_sample.im = (float)raw_q / 32000.0f;

        channel_sim_process(&channel, &clean_sample, &corrupted_sample);
        wav_write_sample(&wav_rx, &corrupted_sample);
    }

    // --- ЭТАП 3.2: Преамбула Баркера (11 DBPSK-символов) ---
    // Используем классический код Баркера-11: 1, 1, 1, -1, -1, -1, 1, -1, -1, 1, -1
    // Классический код Баркера-11 (абсолютные знаки): 1, 1, 1, -1, -1, -1, 1, -1, -1, 1, -1
    // int barker_signs[11] = {1, 1, 1, -1, -1, -1, 1, -1, -1, 1, -1};
    // 1. Массив знаков MLS-31 (вычислен через полином x^5 + x^2 + 1)
    // Содержит ровно 31 элемент (15 единиц и 16 минус единиц — идеальный баланс)
#define SIGNATURE_LENGTH	31
    const int mls_31_signs[SIGNATURE_LENGTH] = {
        1,  1,  1,  1,  1, -1, -1,  1,  1, -1,
        1, -1,  1,  1,  1, -1,  1, -1, -1, -1,
        1, -1, -1,  1, -1,  1, -1,  1, -1, -1, -1
    };
    // 2. Код передатчика в Вашей модели симулятора
    printf("[ПЕРЕДАТЧИК] Генерация честной кумулятивной DBPSK...\n");

    unsigned short current_absolute_phase = current_absolute_synchodummy_phase; // фааза синхропустышки
    unsigned short phase_step;

    for (int sym = 0; sym < SIGNATURE_LENGTH; sym++) {
        // Вычисляем дельту для текущего шага
        phase_step = (mls_31_signs[sym] == 1) ? 8192 : 57344;

        // Кумулятивно накапливаем абсолютную фазу символа
        current_absolute_phase = (unsigned short)(current_absolute_phase + phase_step);

        for (int s = 0; s < 800; s++) {
            short raw_i, raw_q;

            // Передаем ТЕКУЩУЮ СТАБИЛЬНУЮ ФАЗУ символа на протяжении всех 800 сэмплов!
            dqpsk_synth_tick(&modulator, current_absolute_phase, &raw_i, &raw_q);

            clean_sample.re = (float)raw_i / 32000.0f;
            clean_sample.im = (float)raw_q / 32000.0f;

            channel_sim_process(&channel, &clean_sample, &corrupted_sample);
            wav_write_sample(&wav_rx, &corrupted_sample);
        }
    }

    // --- ПОВТОРЕНИЕ ЭТАПА 2: Пилот-тон (Чистая несущая 1000 Гц без модуляции) ---
    for (int i = 0; i < pilot_samples; i++) {
        short raw_i, raw_q;
        dqpsk_synth_tick(&modulator, current_absolute_phase, &raw_i, &raw_q); // остаёмся на той фазе, где остановились при передаче преамбулы
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
		SEARCHING_PREAMBLE,
		MEASURING_NEEDLE,
		GARBLING
    } test_mode_t;

    test_mode_t mode = SEARCHING_PREAMBLE;//SEARCHING_PILOT;
    float detected_freq_error = 0.0f;
	FILE *csv = fopen ("spectrum.csv", "wt");
	FILE *mls31_csv = fopen ("mls31.csv", "wt");
	//fprintf (csv, "Sample,Correlation,NoiseFloor\n");
    fft_init_tables();
    int got_barker=0;

    // Переменные состояния детектора (вынести в rx_state структуры)
    int barker_lock_triggered = 0;
    int window_counter = 0;
    float ultimate_max_power = 0.0f;
    int ultimate_max_step = 0;

    //clk_detect_t clk_sync;
    //clk_detect_init(&clk_sync);

    //schmidl_cox_t sc_sync;
    //sc_init(&sc_sync);

    mls_sync_t mls_sync;
    mls_init(&mls_sync);

	#define NEEDLE_THRESHOLD 0.50f
	#define CONST_GROUP_DELAY 50 // Наша расчетная задержка дециматора

	static int t_start = 0;
	static int t_stop = 0;

    while (wav_read_sample(&wav_in, &rx_sample) == 1) {
    	/*
    	1) check_spectrum_for_pilot
    	2) coarse_mixer_process
    	3) fir_filter_process
    	*/
    	float needle_val = 0.0f;
    	float sc_power = 0.0f;
    	int is_synchronised = mls_tick(&mls_sync, &rx_sample, &needle_val, &sc_power);
    	fprintf(mls31_csv, "%d, %.4f, %.4f, %.4f\n", b_step, mls_sync.spy, needle_val, cplx_phase(mls_sync.derot)/3.1415);

    	if (mode == SEARCHING_PREAMBLE) {
    		//float sc_power = 0.0f;
			//cplx_f32 sc_complex = {0.0f, 0.0f};

    		//sc_tick(&sc_sync, &rx_sample, &sc_power, &sc_complex);

			// Пишем лог: отсчет, нормированная мощность, вещественная часть узора
    	    // Функция возвращает 1 строго в момент идеальной тактовой засечки!


			if (b_step == 8000) { /* тупая симуляция захвата частоты Костасом посреди пилот-тона */
				mls_sync.is_calibrated = 1;
				mls_sync.calibre.re = mls_sync.running_sum.re;
				mls_sync.calibre.im = mls_sync.running_sum.im;
			};

	        if (needle_val > NEEDLE_THRESHOLD) {
	            t_start = b_step; // Запомнили точку входа на склон
	            mode = MEASURING_NEEDLE;
	        }

			// Пишем лог: b_step, мощность Шмидля-Кокса, и ИГЛА второй ступени
			//fprintf(mls31_csv, "%d, %.4f, %.4f\n", b_step, sc_power, mls_needle);
			//fprintf(mls31_csv, "%d, %.4f, %.4f\n", b_step, sc_power, 180.0*cplx_phase(mls_sync.derot)/3.1415);

            //wav_write_sample(&wav_out, &rx_sample);
    	} else if (mode == MEASURING_NEEDLE) {
            if (needle_val < NEEDLE_THRESHOLD) {
                t_stop = b_step; // Запомнили точку выхода со склона

                // Вычисляем геометрический центр купола
                int center_of_dome = (t_start + t_stop) / 2;

                // Финальная привязка к сетке полезной нагрузки с учетом задержки фильтров
                int true_viterbi_sync_point = center_of_dome - CONST_GROUP_DELAY;

                printf("[СИНХРОНИЗАЦИЯ] Игла зафиксирована! Истинный конец преамбулы: %d\n", true_viterbi_sync_point);
                printf("                Конец преамбулы в исходном сигнале (модель): %u\n", preamble_end_sample);

                // Вычисляем точную погрешность относительно ожидаемого значения симулятора
                int error = true_viterbi_sync_point - preamble_end_sample;
                printf("[СИНХРОНИЗАЦИЯ] Погрешность системы: %d сэмплов\n", error);

                // Сбрасываем тактовый генератор символов Витерби в 0 и погнали принимать данные!
                //symbol_sample_counter = 0;
                mode = GARBLING;
            }
    	} else if (mode == GARBLING)  {
    		/* just do nothing till end of input file */
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
    ///printf ("Тест RX-конвейера завершён, сигнатура Баркера %s\n", (got_barker == 1)?"НАЙДЕНА!" : "не обнаружена. Провал!");
    printf ("Тест RX-конвейера завершён\n");
	fclose(csv);
	fclose(mls31_csv);
    wav_close(&wav_in);
    wav_close(&wav_out);

    return 0;
}
