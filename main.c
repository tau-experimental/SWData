#include "config.h"
#include "tx.h"
#include "rx.h"
#include "wav_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Эмулятор КВ канала: добавляет сдвиг частоты и шум
int16_t apply_kv_channel(int16_t input_sample, float freq_shift_hz, uint32_t sample_idx) {
    float signal = (float)input_sample / 16384.0f; // Переводим обратно во float

    // 1. Симулируем дрейф частоты гетеродина
    float t = (float)sample_idx / FS;
    float phase_shift = 2.0f * PI_F * freq_shift_hz * t;

    // В реальности КВ-сдвиг частоты «крутит» фазу вещественного сигнала
    float shifted_signal = signal * cosf(phase_shift);

    // 2. Добавляем небольшой КВ шум (AWGN)
    // Упрощенный генератор шума от -0.05 до +0.05
    float noise = (((float)rand() / (float)RAND_MAX) - 0.5f) * 0.1f;
    shifted_signal += noise;

    // Ограничение (clipping), чтобы не выйти за границы int16_t
    if (shifted_signal > 1.0f) shifted_signal = 1.0f;
    if (shifted_signal < -1.0f) shifted_signal = -1.0f;

    return (int16_t)(shifted_signal * 16384.0f);
}

int main(void) {
    printf("--- Запуск тестирования физического уровня КВ-модема ---\n");

    tx_init();

    // Открываем файл для записи выходного радиосигнала
    FILE *wav_file = wav_open_write("transmission.wav", FS);
    if (!wav_file) {
        printf("Ошибка: не удалось создать WAV файл!\n");
        return -1;
    }

    // Параметры симуляции
    float simulated_frequency_drift = 30.0f; // Передатчик «уплыл» вверх на 30 Гц
    printf("[Канал] Задана погрешность частоты: +%.1f Гц\n", simulated_frequency_drift);

    // Генерируем преамбулу Шмидла-Кокса (длина = 2 символа для надежного захвата)
    uint32_t total_samples = N_SAMPLES * 2;
    printf("[Передатчик] Генерация %d сэмплов преамбулы (тоны %.0f и %.0f Гц)...\n",
            total_samples, FREQ_TONE1, FREQ_TONE2);

    for (uint32_t i = 0; i < total_samples; i++) {
        // Получаем чистый сэмпл от модулирующего движка
        int16_t clean_sample = tx_get_next_sample(TX_STATE_PREAMBLE, 0);

        // Пропускаем через искажения КВ-эфира
        int16_t dirty_sample = apply_kv_channel(clean_sample, simulated_frequency_drift, i);

        // Записываем в WAV
        fwrite(&dirty_sample, sizeof(int16_t), 1, wav_file);
    }

    wav_close_write(wav_file);
    printf("[Тест] Файл 'transmission.wav' успешно записан. Проверьте его структуру.\n");

    return 0;
}
