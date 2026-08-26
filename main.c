#include "config.h"
#include "tx.h"
#include "rx.h"
#include "wav_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Эмулятор КВ канала: добавляет сдвиг частоты и шум
void apply_kv_channel_iq(complex_f *input, float freq_shift_hz, uint32_t sample_idx, int16_t *out_i, int16_t *out_q) {
    float t = (float)sample_idx / FS;
    float phase = 2.0f * PI_F * freq_shift_hz * t;

    // Комплексный поворот фазы
    float i_rot = input->re * cosf(phase) - input->im * sinf(phase);
    float q_rot = input->re * sinf(phase) + input->im * cosf(phase);

    // Добавляем независимый шум в каждый канал
    i_rot += (((float)rand() / (float)RAND_MAX) - 0.5f) * 0.05f;
    q_rot += (((float)rand() / (float)RAND_MAX) - 0.5f) * 0.05f;

    *out_i = (int16_t)(i_rot * 16384.0f);
    *out_q = (int16_t)(q_rot * 16384.0f);
}

int main(void) {
    srand(time(NULL));
    printf("=== Сквозной тест КВ-модема: Преамбула + CP + OFDM Данные ===\n");

    tx_init();
    FILE *wav_file_out = wav_open_write("transmission.wav", FS);
    if (!wav_file_out) {
        printf("Ошибка: не удалось создать выходной WAV-файл!\n");
        return -1;
    }

    // Случайные КВ-условия для этого прогона
    float simulated_frequency_drift = ((float)(rand() % 60) - 30.0f); // Сдвиг от -30 до +30 Гц
    uint32_t random_silence_samples = 200 + (rand() % 800);                   // Пауза от 200 до 1000 сэмплов

    // Задаем тестовую полезную нагрузку: 4 бита = 0x0D (двоичное: 1101)
    uint8_t test_nibble = 0x0D;
    tx_set_data_nibble(test_nibble);

    uint32_t global_sample_counter = 0;
    int16_t iq_frame[2];

    printf("[Канал] Слепая пауза до старта: %u сэмплов.\n", random_silence_samples);
    printf("[Канал] Искусственный дрейф гетеродина: %.2f Гц\n", simulated_frequency_drift);

    // 1. ГЕНЕРАЦИЯ: Начальная тишина (только шум эфира)
    complex_f zero_signal = {0.0f, 0.0f};
    for (uint32_t i = 0; i < random_silence_samples; i++) {
        apply_kv_channel_iq(&zero_signal, simulated_frequency_drift, global_sample_counter, &iq_frame[0], &iq_frame[1]);
        fwrite(iq_frame, sizeof(int16_t), 2, wav_file_out);
        global_sample_counter++;
    }

    // 2. ГЕНЕРАЦИЯ: Преамбула Шмидла-Кокса (2 символа по N)
    uint32_t preamble_samples = N_SAMPLES * 2;
    printf("[Передатчик] Старт преамбулы на сэмпле: %u\n", global_sample_counter);
    for (uint32_t i = 0; i < preamble_samples; i++) {
        complex_f out;
        tx_get_next_iq_sample(TX_STATE_PREAMBLE, &out);
        apply_kv_channel_iq(&out, simulated_frequency_drift, global_sample_counter, &iq_frame[0], &iq_frame[1]);
        fwrite(iq_frame, sizeof(int16_t), 2, wav_file_out);
        global_sample_counter++;
    }

    // 3. ГЕНЕРАЦИЯ: Один OFDM-символ данных (N + CP сэмплов)
    printf("[Передатчик] Старт символа данных (Payload: 0x%X) на сэмпле: %u\n", test_nibble, global_sample_counter);
    for (uint32_t i = 0; i < TOTAL_SYMBOL_SAMPLES; i++) {
        complex_f out;
        tx_get_next_iq_sample(TX_STATE_DATA, &out);
        apply_kv_channel_iq(&out, simulated_frequency_drift, global_sample_counter, &iq_frame[0], &iq_frame[1]);
        fwrite(iq_frame, sizeof(int16_t), 2, wav_file_out);
        global_sample_counter++;
    }

    // 4. ГЕНЕРАЦИЯ: Финальный шум в конце передачи
    for (uint32_t i = 0; i < 200; i++) {
        apply_kv_channel_iq(&zero_signal, simulated_frequency_drift, global_sample_counter, &iq_frame[0], &iq_frame[1]);
        fwrite(iq_frame, sizeof(int16_t), 2, wav_file_out);
        global_sample_counter++;
    }

    wav_close_write(wav_file_out);
    printf("[Передатчик] Сигнал полностью синтезирован в 'transmission.wav'.\n\n");

    // ==========================================
    // ДЕКОДИРОВАНИЕ: РАБОТА ПРИЕМНОГО АВТОМАТА
    // ==========================================
    FILE *wav_file_in = fopen("transmission.wav", "rb");
    if (!wav_file_in) return -1;
    fseek(wav_file_in, 44, SEEK_SET); // Пропускаем RIFF-заголовок

    rx_init();
    uint32_t current_sample_idx = 0;
    uint8_t rx_output_nibble = 0;
    int is_synchronized = 0;

    printf("[Приемник] Начинаем потоковый разбор аудио...\n");
    while (fread(iq_frame, sizeof(int16_t), 2, wav_file_in)) {
        current_sample_idx++;

        // Скармливаем очередную IQ-пару в наш FSM-автомат
        if (rx_process_iq_sample(iq_frame[0], iq_frame[1], &rx_output_nibble)) {
            // Если функция вернула true — автомат успешно отработал RX_STATE_DECODE
            printf("\n[ПРИЕМНИК] Символ данных успешно демодулирован!\n");
            printf("[Приемник] Извлеченный полубайт: 0x%X (в двоичном виде: ", rx_output_nibble);
            for(int b = 3; b >= 0; b--) printf("%d", (rx_output_nibble >> b) & 1);
            printf(")\n");

            if (rx_output_nibble == test_nibble) {
                printf("[РЕЗУЛЬТАТ] ТЕСТ УСПЕШНО ПРОЙДЕН! Ошибок в битах нет.\n");
            } else {
                printf("[РЕЗУЛЬТАТ] ОШИБКА: Данные повреждены! Ожидалось 0x%X\n", test_nibble);
            }
            break;
        }

        // Логируем момент, когда Шмидл-Кокс только ловит преамбулу
        if (!is_synchronized) {
            float offset = rx_get_frequency_offset();
            if (offset != 0.0f) {
                printf("[Приемник] Преамбула зафиксирована на сэмпле: %u\n", current_sample_idx);
                printf("[Приемник] Измеренный КВ-сдвиг: %.2f Гц (Реальный: %.2f Гц)\n", offset, simulated_frequency_drift);
                printf("[Приемник] Автомат перешел в RX_STATE_GUARD (пропуск %d сэмплов префикса)...\n", CP_SAMPLES);
                is_synchronized = 1;
            }
        }
    }

    fclose(wav_file_in);
    return 0;
}
