#ifndef CONFIG_H
#define CONFIG_H

#define FS 8000               // Частота дискретизации АЦП/ЦАП
#define N_SAMPLES 160         // Базовый размер символа (можно менять)
#define HALF_N (N_SAMPLES / 2)

// Частоты тонов преамбулы
#define FREQ_TONE1 1200.0f
#define FREQ_TONE2 1800.0f

#define PI_F 3.14159265f

#endif // CONFIG_H
