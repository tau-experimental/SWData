#ifndef MODULATOR_H
#define MODULATOR_H

#include "wav_io.h"

// Структура состояния модулятора π/4-DQPSK
typedef struct {
    unsigned short phase_acc;   // 16-битный аккумулятор фазы (DDS)
    unsigned short phase_inc;   // Частота настройки (FTW — Frequency Tuning Word) = 8192 для 1000 Гц
    short sine_lut[256];        // Динамическая или статическая таблица синуса
} dqpsk_modulator_t;

// Инициализация модулятора (установка несущей)
void dqpsk_modulator_init(dqpsk_modulator_t *mod, unsigned int carrier_freq, unsigned int sample_rate);

// Функция модуляции пакета данных
// Вход: скремблированные биты (840 плоских бит, упакованных по 1 биту на байт)
// Выход: массив отсчетов звука (размер: 420 символов * 800 отсчетов = 336 000 значений short)
void dqpsk_modulate_packet_v3(dqpsk_modulator_t *mod, const unsigned char *in_bits, wav_stream_t *wav_out);
void dqpsk_synth_tick(dqpsk_modulator_t *mod, unsigned short phase_shift, short *out_i, short *out_q);
unsigned short dqpsk_get_phase_shift(unsigned char b1, unsigned char b2);
void dqpsk_modulate_barker(dqpsk_modulator_t *mod, wav_stream_t *wav_out);

#endif // MODULATOR_H
