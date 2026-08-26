#include "config.h"
#include "tx.h"
#include "rx.h"
#include "wav_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Функция записи сэмплов с эмуляцией джиттера тактовой частоты (Clock Jitter)
void write_sample_with_jitter(int16_t *iq_frame, FILE *file, uint32_t *samples_written) {
    // Генерируем случайное событие джиттера (вероятность 2%)
    int r = rand() % 100;

    if (r == 0) {
        // ЭФФЕКТ 1: Дублирование сэмпла (МК приемника поспешил)
        // Записываем один и тот же кадр дважды
        fwrite(iq_frame, sizeof(int16_t), 2, file);
        fwrite(iq_frame, sizeof(int16_t), 2, file);
        (*samples_written) += 2;
    }
    else if (r == 1) {
        // ЭФФЕКТ 2: Потеря сэмпла (МК приемника опоздал)
        // Просто выбрасываем этот сэмпл, не записывая его в файл
        // (В эфире произошел микропропуск)
    }
    else {
        // Нормальный режим (98% времени)
        fwrite(iq_frame, sizeof(int16_t), 2, file);
        (*samples_written)++;
    }
}

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

// Функция передачи одного полного OFDM-кадра (CP + Данные)
void transmit_single_nibble(uint8_t nibble, float drift, uint32_t *global_tx, int16_t *frame, FILE *file, uint32_t *written) {
    tx_set_data_nibble(nibble);
    for (uint32_t i = 0; i < TOTAL_SYMBOL_SAMPLES; i++) {
        complex_f out;
        tx_get_next_iq_sample(TX_STATE_DATA, &out);
        apply_kv_channel_iq(&out, drift, *global_tx, &frame[0], &frame[1]);
        write_sample_with_jitter(frame, file, written);
        (*global_tx)++;
    }
}

int main(void) {
    srand((unsigned int)time(NULL));
    printf("=== Боевой КВ-тест: Передача текстового сообщения сквозь поток помех ===\n");

    tx_init();
    FILE *wav_file_out = wav_open_write("transmission.wav", FS);
    if (!wav_file_out) return -1;

    float simulated_frequency_drift = ((float)(rand() % 60) - 30.0f);
    uint32_t random_silence_samples = 200 + (rand() % 800);
    uint32_t global_tx_counter = 0;
    uint32_t real_file_samples = 0;
    int16_t iq_frame[2];

    // Наше текстовое сообщение (SMS)
    const char *text_message = "HELLO RISCV";
    uint32_t text_len = strlen(text_message);

    // 1. ГЕНЕРАЦИЯ: Пауза шума
    complex_f zero_signal = {0.0f, 0.0f};
    for (uint32_t i = 0; i < random_silence_samples; i++) {
        apply_kv_channel_iq(&zero_signal, simulated_frequency_drift, global_tx_counter, &iq_frame[0], &iq_frame[1]);
        write_sample_with_jitter(iq_frame, wav_file_out, &real_file_samples);
        global_tx_counter++;
    }

    // 2. ГЕНЕРАЦИЯ: Преамбула Шмидла-Кокса
    uint32_t preamble_samples = N_SAMPLES * 2;
    for (uint32_t i = 0; i < preamble_samples; i++) {
        complex_f out;
        tx_get_next_iq_sample(TX_STATE_PREAMBLE, &out);
        apply_kv_channel_iq(&out, simulated_frequency_drift, global_tx_counter, &iq_frame[0], &iq_frame[1]);
        write_sample_with_jitter(iq_frame, wav_file_out, &real_file_samples);
        global_tx_counter++;
    }

    // 3. ГЕНЕРАЦИЯ: "Гарпун" DPSK (Опорный символ-маяк)
    transmit_single_nibble(0x00, simulated_frequency_drift, &global_tx_counter, iq_frame, wav_file_out, &real_file_samples);

    // 4. ГЕНЕРАЦИЯ: Потоковый перевод текста в OFDM символы
    printf("[Передатчик] Отправка текста: \"%s\" (%u букв)\n", text_message, text_len);
    for (uint32_t i = 0; i < text_len; i++) {
        uint8_t ascii_code = (uint8_t)text_message[i];

        // Разбиваем байт буквы на два полубайта: сначала младший, потом старший
        uint8_t low_nibble = ascii_code & 0x0F;
        uint8_t high_nibble = (ascii_code >> 4) & 0x0F;

        // Передаем два OFDM символа подряд для одной буквы
        transmit_single_nibble(low_nibble, simulated_frequency_drift, &global_tx_counter, iq_frame, wav_file_out, &real_file_samples);
        transmit_single_nibble(high_nibble, simulated_frequency_drift, &global_tx_counter, iq_frame, wav_file_out, &real_file_samples);
    }

    // 5. ГЕНЕРАЦИЯ: Маркер Конца Передачи (EOT = 0x0F)
    transmit_single_nibble(0x0F, simulated_frequency_drift, &global_tx_counter, iq_frame, wav_file_out, &real_file_samples);

    // 6. ГЕНЕРАЦИЯ: завершающая шумовая дорожка
    random_silence_samples = 200 + (rand() % 800);
    for (uint32_t i = 0; i < random_silence_samples; i++) {
        apply_kv_channel_iq(&zero_signal, simulated_frequency_drift, global_tx_counter, &iq_frame[0], &iq_frame[1]);
        write_sample_with_jitter(iq_frame, wav_file_out, &real_file_samples);
        global_tx_counter++;
    }

    wav_close_write(wav_file_out);
    printf("[Канал] Файл сгенерирован сквозь стохастический джиттер.\n\n");

    // ==========================================
    // ПОТОКОВОЕ ДЕКОДИРОВАНИЕ ПРИЕМНИКОМ
    // ==========================================
    FILE *wav_file_in = fopen("transmission.wav", "rb");
    if (!wav_file_in) return -1;
    fseek(wav_file_in, 44, SEEK_SET);

    rx_init();
    uint8_t rx_nibble = 0;

    // Переменные для сборки букв из нибблов
    uint8_t accumulated_ascii = 0;
    int nibble_toggle = 0;

    printf("[Приемник] Потоковый разбор эфира. Принимаемый текст: ");

    while (fread(iq_frame, sizeof(int16_t), 2, wav_file_in)) {
        if (rx_process_iq_sample(iq_frame[0], iq_frame[1], &rx_nibble)) {
            // Если это маркер EOT, завершаем поток
            if (rx_nibble == 0x0F) {
                printf("\n[Приемник] Поток успешно завершен по маркеру EOT.\n");
                break;
            }

            // Собираем байты из полубайтов
            if (nibble_toggle == 0) {
                accumulated_ascii = rx_nibble; // Сохраняем младший полубайт
                nibble_toggle = 1;
            } else {
                accumulated_ascii |= (rx_nibble << 4); // Добавляем старший полубайт
                nibble_toggle = 0;

                // Выводим принятую букву на экран прямо в поток консоли!
                printf("%c", (char)accumulated_ascii);
                fflush(stdout);
            }
        }
    }

    fclose(wav_file_in);
    return 0;
}

