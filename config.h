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

// Частоты тонов для данных (OFDM сетка)
#define NUM_DATA_TONES 4
static const float data_tones[NUM_DATA_TONES] = {1000.0f, 1200.0f, 1400.0f, 1600.0f};

#define PI_F 3.14159265f
#define SILENCE_SAMPLES 320

// Структура комплексного числа для ЦОС
typedef struct {
    float re;
    float im;
} complex_f;

#endif // CONFIG_H
