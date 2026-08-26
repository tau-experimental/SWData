#include "config.h"
#include "tx.h"
#include "rx.h"
#include "wav_io.h"
#include <stdio.h>

// Набор тестовых нибблов для проверки физики
#define TEST_SUITE_SIZE 6
static const uint8_t test_sequence[TEST_SUITE_SIZE] = {
    0x0, // Сплошные нули (все фазы шагают назад на -90)
    0x0, // Снова нули (проверка слепоты приёмника)
    0xF, // Сплошные единицы (все фазы шагают вперед на +90)
    0xF, // Снова единицы
    0x5, // Каша 0101
    0xA  // Каша 1010
};

int main(void) {
    printf("=== ЛАБОРАТОРНЫЙ СТЕНД: ПРОВЕРКА КВАДРАТУРНОГО ШАГА DDS/DPSK ===\n");

    tx_init();
    rx_init();

    FILE *wav_out = wav_open_write("laboratory.wav", FS);
    complex_f data_buffer[N_SAMPLES];

    // Шагаем по нашей тестовой последовательности символов
    for (int smb = 0; smb < TEST_SUITE_SIZE; smb++) {
        uint8_t target_nibble = test_sequence[smb];

        // 1. ПЕРЕДАТЧИК: переключает фазы DDS для текущего ниббла
        tx_step_phase(target_nibble);

        printf("\n--- СИМВОЛ №%d: Передаём ниббл 0x%X ---\n", smb + 1, target_nibble);

        for (int i = 0; i < TOTAL_SYMBOL_SAMPLES; i++) {
            complex_f sample;

            // ВЫЗЫВАЕМ ЧИСТЫЙ DDS ГЕНЕРАТОР:
            tx_get_sample(&sample);

            // Пишем стерео-сэмпл в WAV для визуального контроля
            int16_t wav_frame[2];
            wav_frame[0] = (int16_t)(sample.re * 16384.0f);
            wav_frame[1] = (int16_t)(sample.im * 16384.0f);
            fwrite(wav_frame, sizeof(int16_t), 2, wav_out);

            // Приёмник копит в ДПФ-буфер только чистые сэмплы тела
            if (i >= CP_SAMPLES) {
                data_buffer[i - CP_SAMPLES] = sample;
            }
        }

        // 3. ДЕМОДУЛЯЦИЯ: Запускаем чистый ДПФ-анализ накопленного буфера
        uint8_t rx_nibble = rx_decode_symbol(data_buffer, smb);

        if (smb > 0) { // Пропускаем первый стартовый символ-калибровку
            printf("[РЕЗУЛЬТАТ] Передано: 0x%X | Принято: 0x%X -> %s\n",
                   target_nibble, rx_nibble, (target_nibble == rx_nibble) ? "ИДЕАЛЬНО" : "ОШИБКА");
        }
    }

    wav_close_write(wav_out);
    return 0;
}
