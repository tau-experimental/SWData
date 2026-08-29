#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "conv_encoder.h"
#include "puncturing.h"

// Генерация псевдослучайных бит (0 или 1) для чистоты эксперимента
static void generate_random_bits(unsigned char *bits, int len, unsigned int seed) {
    srand(seed);
    for (int i = 0; i < len; i++) {
        bits[i] = rand() % 2;
    }
}

int main(void) {
    printf("=== СТАРТ СКВОЗНОГО СТРЕСС-ТЕСТА ЦИФРОВОГО ТРАКТА (v3.0) ===\n\n");

    // 1. Выделение памяти под массивы (статически/на стеке ПК-стенда)
    unsigned char tx_payload_bits[840+16];
    unsigned char tx_encoded_1_2[1680+16];
    unsigned char tx_punctured_5_6[1008+16];

    unsigned char rx_depunctured_soft[1680+16];
    unsigned char rx_decoded_bytes[105+16]; // 840 бит / 8 = 105 байт
    unsigned char rx_decoded_bits[840+16];

    conv_encoder_t encoder;
    viterbi_init_tables();

    // Генерируем тестовую последовательность (последние 6 бит ОБЯЗАТЕЛЬНО нули для Zero-Tail)
    generate_random_bits(tx_payload_bits, 840, 0xACE);
    for (int i = 834; i < 840; i++) {
        tx_payload_bits[i] = 0;
    }

    printf("[TX] Шаг 1: Сверточное кодирование Rate 1/2...\n");
    conv_encoder_reset(&encoder);
    conv_encode_pure_1_2(&encoder, tx_payload_bits, tx_encoded_1_2);
    printf("[TX] OK: Сгенерировано %d плоских бит.\n", 1680);

    // Контрольный чих №1: Вывод первых 16 бит после кодера
    printf("[ОТЛАДКА] Первые 16 кодированных бит: ");
    for(int i=0; i<16; i++) printf("%d", tx_encoded_1_2[i]);
    printf("\n\n");
    printf("\n--- [ИНСПЕКЦИЯ ВЫХОДА КОДЕРА] ---\n");
    // Моделируем состояние регистра кодера локально для шагов 720-723
    unsigned char local_reg = 0;
    // Сначала быстро «прокрутим» регистр до 719 шага, чтобы он встал в нужную фазу
    for (int s = 0; s < 720; s++) {
        local_reg = ((local_reg << 1) | tx_payload_bits[s]) & 0x3F;
    }

    // Теперь детально проверяем шаги 720-723
    for (int s = 720; s <= 723; s++) {
        unsigned char bit = tx_payload_bits[s];
        // Находим теоретический дибит по полиномам NASA (сдвиг влево)
        // Внимание: используем логику вашей инициализации таблиц
        int next_state = ((local_reg << 1) | bit) & 0x3F;

        // Читаем, что РЕАЛЬНО записал кодер в плоский массив tx_encoded_1_2
        unsigned char real_g1 = tx_encoded_1_2[s * 2];
        unsigned char real_g2 = tx_encoded_1_2[s * 2 + 1];

        // Читаем, что для этого перехода прописано в таблице out_bits
        unsigned char packed_dibit = out_bits[local_reg][bit];
        unsigned char table_g1 = (packed_dibit >> 1) & 1;
        unsigned char table_g2 = packed_dibit & 1;

        printf("Шаг %d (Инфо-бит %d): Текущий регистр кодера = %d\n", s, bit, local_reg);
        printf("         Из массива tx_encoded_1_2: G1=%d, G2=%d\n", real_g1, real_g2);
        printf("         Из таблицы out_bits:       G1=%d, G2=%d\n", table_g1, table_g2);

        // Обновляем регистр для следующего шага
        local_reg = next_state;
    }
    printf("----------------------------------\n\n");

    printf("[TX] Шаг 2: Применение выкалывания (Puncturing) 1680 -> 1008...\n");
    apply_puncturing(tx_encoded_1_2, tx_punctured_5_6);
    printf("[TX] OK: Поток упакован в %d бит для эфира.\n\n", 1008);


    // ================= КАНАЛ СВЯЗИ С ОШИБКАМИ =================
    printf("[КАНАЛ] Вносим 5 изолированных инверсий бит в выколотый поток (Стресс-тест)...\n");
    // Переворачиваем биты в разных частях пакета
    tx_punctured_5_6[10]  ^= 1;
    tx_punctured_5_6[200] ^= 1;
    tx_punctured_5_6[500] ^= 1;
    tx_punctured_5_6[750] ^= 1;
    tx_punctured_5_6[990] ^= 1;
    printf("[КАНАЛ] OK: Ошибки добавлены.\n\n");
    // ==========================================================


    printf("[RX] Шаг 3: Восстановление выколотых бит (Depuncturing) -> Софт-биты...\n");
    apply_depuncturing(tx_punctured_5_6, rx_depunctured_soft);
    printf("[RX] OK: Сформировано %d мягких отсчетов.\n", 1680);

    // Контрольный чих №2: Проверяем, как депунктуризатор пометил стертые биты и ошибки
    printf("[ОТЛАДКА] Первые 10 мягких отсчетов (ищите 127 для выколотых): ");
    for(int i=0; i<10; i++) printf("%d ", rx_depunctured_soft[i]);
    printf("\n\n");

    printf("[RX] Шаг 4: Запуск мягкого декодера Витерби (Zero-Tail, O(1) Traceback)...\n");
    viterbi_decode_soft_1_2(rx_depunctured_soft, rx_decoded_bytes);
    //viterbi_debug (tx_payload_bits, rx_depunctured_soft, rx_decoded_bytes);
    printf("[RX] OK: Декодирование завершено.\n\n");

    // Распаковываем байты обратно в биты для побитового сравнения 1-в-1
    for (int step = 0; step < 840; step++) {
        int byte_pos = step / 8;
        int bit_pos = 7 - (step % 8);
        rx_decoded_bits[step] = (rx_decoded_bytes[byte_pos] >> bit_pos) & 1;
    }

    // ВЕРИФИКАЦИЯ: Сравнение исходного массива с восстановленным
    printf("=== ВЕРИФИКАЦИЯ РЕЗУЛЬТАТОВ ===\n");
    int error_coords[32];
    int total_errors = 0;

    for (int i = 0; i < 840; i++) {
        if (tx_payload_bits[i] != rx_decoded_bits[i]) {
            if (total_errors < 32) {
                error_coords[total_errors] = i;
            }
            total_errors++;
        }
    }

    if (total_errors == 0) {
        printf("🎉 ИДЕАЛЬНО! Витерби сожрал 5 КВ-ошибок и восстановил данные со 100%% точностью!\n");
        printf("Цифровой кодек полностью стабилен и готов к сопряжению с радио-модулятором.\n");
    } else {
        printf("❌ ТЕСТ ПРОВАЛЕН! Найдено %d ошибок декодирования.\n", total_errors);
        printf("[КООРДИНАТЫ ОШИБОК]: ");
        for (int i = 0; i < (total_errors < 32 ? total_errors : 32); i++) {
            printf("%d ", error_coords[i]);
        }
        printf("\n\n[СОВЕТ]: Если упало в самом начале — проверяйте инициализацию метрик. Если в конце — разберитесь с шагом Traceback или Zero-Tail.");
        return -1;
    }

    return 0;
}
