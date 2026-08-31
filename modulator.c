#include "modulator.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

void dqpsk_modulator_init(dqpsk_modulator_t *mod, unsigned int carrier_freq, unsigned int sample_rate) {
    mod->phase_acc = 0;

    // Формула шага фазы: (carrier_freq * 65536) / sample_rate
    // Для 1000 Гц и 8000 Гц: (1000 * 65536) / 8000 = 65536 / 8 = 8192
    mod->phase_inc = (unsigned short)(((unsigned long long)carrier_freq << 16) / sample_rate);

    int i;
    FILE *sintab = fopen("sintab.txt", "wt");
    for (i = 0; i < 256; i++) {
    	double x = (i * 2.0 * M_PI) / 256.0;
    	mod->sine_lut[i] = (short)(32000.0 * sin(x));
    	fprintf (sintab, "%u, %d\n", i, mod->sine_lut[i]);
    }
    fclose(sintab);
}

// Вызывается внутри прерывания таймера 8 кГц
void dqpsk_synth_tick(dqpsk_modulator_t *mod, unsigned short symbol_phase, short *out_i, short *out_q) {
    // 1. Частотный аккумулятор накапливает ТОЛЬКО чистую несущую (FTW)
    mod->phase_acc = (unsigned short)(mod->phase_acc + mod->phase_inc);

    // 2. Полная фаза для таблицы синусов — это сумма частотного аккумулятора
    // и ТЕКУЩЕЙ абсолютной фазы символа
    unsigned short total_phase = (unsigned short)(mod->phase_acc + symbol_phase);

    // 3. Извлекаем индексы из полной фазы
    unsigned char sin_idx = (unsigned char)(total_phase >> 8);
    unsigned char cos_idx = (unsigned char)((sin_idx + 64) & 0xFF);

    *out_i = mod->sine_lut[cos_idx];
    *out_q = mod->sine_lut[sin_idx];
}

void dqpsk_modulate_packet_v3(dqpsk_modulator_t *mod, const unsigned char *in_bits, wav_stream_t *wav_out) {
    // Идем по 420 символам пакета
    for (int sym = 0; sym < 420; sym++) {
        // 1. Работает задатчик частоты (раз в символ): кодируем дибит по Грею
        unsigned short phase_shift = dqpsk_get_phase_shift(in_bits[sym * 2], in_bits[sym * 2 + 1]);

        // 2. Запускаем 800 отсчетов символа (10 Бод)
        for (int sample_idx = 0; sample_idx < 800; sample_idx++) {
            short raw_i, raw_q;

            // На самом первом отсчете символа подмешиваем фазовый сдвиг задатчика.
            // На остальных 799 отсчетах фазовая поправка строго равна 0 (несущая идет непрерывно)
            unsigned short current_shift = (sample_idx == 0) ? phase_shift : 0;

            // Тикаем боевым DDS-синтезатором
            dqpsk_synth_tick(mod, current_shift, &raw_i, &raw_q);

            // Конвертируем в формат float для вашего WAV-интерфейса
            cplx_f32 iq_sample;
            iq_sample.re = (float)raw_i / 32000.0f;
            iq_sample.im = (float)raw_q / 32000.0f;

            wav_write_sample(wav_out, &iq_sample);
        }
    }
}
// Задатчик фазы: принимает дибит, возвращает фазовую поправку для DDS
unsigned short dqpsk_get_phase_shift(unsigned char b1, unsigned char b2) {
    if (b1 == 0 && b2 == 0) return 8192;   // +pi/4
    if (b1 == 0 && b2 == 1) return 24576;  // +3pi/4
    if (b1 == 1 && b2 == 1) return 40960;  // -3pi/4
    if (b1 == 1 && b2 == 0) return 57344;  // -pi/4
    return 0;
}

void dqpsk_modulate_barker(dqpsk_modulator_t *mod, wav_stream_t *wav_out) {
    // 6 дибитов, кодирующих 11-битный код Баркера + 1 выравнивающий ноль
    // Дибиты: 11, 10, 00, 10, 01, 00
    unsigned char barker_bits[12] = {1,1, 1,0, 0,0, 1,0, 0,1, 0,0};

    for (int sym = 0; sym < 6; sym++) {
        unsigned short phase_shift = dqpsk_get_phase_shift(barker_bits[sym * 2], barker_bits[sym * 2 + 1]);

        for (int sample_idx = 0; sample_idx < 800; sample_idx++) {
            short raw_i, raw_q;
            unsigned short current_shift = (sample_idx == 0) ? phase_shift : 0;

            dqpsk_synth_tick(mod, current_shift, &raw_i, &raw_q);

            cplx_f32 iq_sample;
            iq_sample.re = (float)raw_i / 32000.0f;
            iq_sample.im = (float)raw_q / 32000.0f;
            wav_write_sample(wav_out, &iq_sample);
        }
    }
}


