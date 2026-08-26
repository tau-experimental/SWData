#ifndef CONFIG_H
#define CONFIG_H

#define FS 8000
#define N_SAMPLES 160
#define HALF_N (N_SAMPLES / 2)
// ЦИКЛИЧЕСКИЙ ПРЕФИКС (ЗАЩИТНЫЙ ИНТЕРВАЛ)
#define CP_SAMPLES 32         // 4 мс защиты от ионосферного эха
#define TOTAL_SYMBOL_SAMPLES (N_SAMPLES + CP_SAMPLES) // Полный размер кадра в эфире (192 сэмпла)

#define FREQ_TONE1 1200.0f
#define FREQ_TONE2 1800.0f

#define SINE_TABLE_SIZE 256
#define PHASE_180_SHIFT 128
#define PHASE_90_SHIFT 64

// Частоты тонов для данных (OFDM сетка)
#define NUM_DATA_TONES 4
extern const float data_tones[NUM_DATA_TONES];

// ПАРАМЕТРЫ ПРЕАМБУЛЫ
#define PREAMBLE_SYMBOLS 10   // Длина преамбулы в символах
#define PREAMBLE_PATTERN 0xA5 // Паттерн "качания" фаз (10100101)

#define PI_F 3.14159265f
#define SILENCE_SAMPLES 320

// Структура комплексного числа для ЦОС
typedef struct {
    float re;
    float im;
} complex_f;

#endif // CONFIG_H
