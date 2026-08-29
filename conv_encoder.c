#include "conv_encoder.h"
#include <string.h>
#include <stdio.h>

void conv_encoder_reset(conv_encoder_t *enc) {
    enc->reg = 0;
}
static int viterbi_inited = 0;
static unsigned char next_state[NUM_STATES][2];
unsigned char out_bits[NUM_STATES][2];
static unsigned char prev_state[NUM_STATES][2];

void viterbi_init_tables(void) {
    int s, b, j;
    if (viterbi_inited) return;

    for (s = 0; s < NUM_STATES; s++) {
        for (b = 0; b < 2; b++) {
            /* Прямой переход: сдвиг влево, новый бит заходит справа */
            int next = ((s << 1) | b) & 0x3F;
            next_state[s][b] = (unsigned char)next;

            /* Обратный переход: запоминаем, что из состояния 's' по биту 'b' мы пришли в 'next' */
            prev_state[next][b] = (unsigned char)s;

            unsigned char reg = (unsigned char)((s << 1) | b);
            unsigned char g1 = 0, g2 = 0;
            for (j = 0; j < 7; j++) {
                if ((POLY_G1 >> j) & 1) g1 ^= (reg >> j) & 1;
                if ((POLY_G2 >> j) & 1) g2 ^= (reg >> j) & 1;
            }
            out_bits[s][b] = (unsigned char)((g1 << 1) | g2);
        }
    }
    viterbi_inited = 1;
}

/* Чистый кодер Rate 1/2. На входе 840 бит, на выходе строго 1680 бит */
void conv_encode_pure_1_2(conv_encoder_t *enc, const unsigned char *in_bits, unsigned char *out_bits_1_2) {
    int i, j, write_ptr = 0;
    for (i = 0; i < 840; i++) {
        unsigned char bit = in_bits[i];

        // 1. Формируем ПОЛНОЕ 7-битное состояние (6 бит истории + 1 новый бит)
        // Новый бит заходит справа, точно так же, как мы считали в таблицах!
        unsigned int state7 = ((enc->reg << 1) | bit) & 0x7F;

        // 2. Считаем полиномы NASA по честной 7-битной маске
        // POLY_G1 = 0x6D (1101101), POLY_G2 = 0x4F (1001111)
        unsigned char g1 = 0;
        unsigned char g2 = 0;

        // Аппаратный подсчет четности (Parity) для G1
        unsigned int s1 = state7 & 0x6D;
        while (s1) { g1 ^= (s1 & 1); s1 >>= 1; }

        // Аппаратный подсчет четности для G2
        unsigned int s2 = state7 & 0x4F;
        while (s2) { g2 ^= (s2 & 1); s2 >>= 1; }

        // 3. Записываем результат в плоский массив
        out_bits_1_2[i * 2]     = g1;
        out_bits_1_2[i * 2 + 1] = g2;

        // 4. И только ТЕПЕРЬ сдвигаем историю в регистре кодера для следующего шага
        enc->reg = state7 & 0x3F; // Оставляем только 6 бит памяти

#if 0
        if ((i >= 719) && (i < 725)) {
        	printf (">> Шпионский перехват из conv_encode_pure_1_2: i == %u, enc->reg = 0x%02X, g1 = %d, g2 = %d\n", i, enc->reg, g1, g2);
        }
#endif
    }
}

#if 0
void viterbi_debug (const unsigned char * tx_payload_bits, const unsigned char *in_soft_bits_1_2, unsigned char *out_bytes_p) {
	// Локальный буфер в функции отладки для хранения истинной траектории кодера
	static unsigned char true_states_history[840];
	unsigned char encoder_sim_reg = 0; // Начинаем с нуля, как и реальный кодер

	// Заполняем историю истинных состояний перед запуском прямого хода Витерби
	for (int step = 0; step < 840; step++) {
	    // Моделируем логику кодера из conv_encode_pure_1_2:
        unsigned char current_bit = tx_payload_bits[step];
        unsigned char old_reg = encoder_sim_reg;

        // Буквальный сдвиг
        encoder_sim_reg = ((old_reg << 1) | current_bit) & 0x3F;
        true_states_history[step] = encoder_sim_reg;

        // Печатаем первые 5 шагов, чтобы поймать момент, почему регистр не обновляется
        if (step < 5) {
            printf("Шаг %d: Бит=%d | Был регистр=%d -> Стал регистр=%d (Записано в history=%d)\n",
                   step, current_bit, old_reg, encoder_sim_reg, true_states_history[step]);
        }
	}
    // === КОНТРОЛЬНЫЙ ЧИХ №1: ВЕРИФИКАЦИЯ ТОПОЛОГИИ ТАБЛИЦ ===
    printf("\n--- [ОТЛАДКА ТАБЛИЦ] Сверка кодера и таблиц Витерби ---\n");

    // Возьмем первые 5 шагов для ручной инспекции глазами
    unsigned char current_state_sim = 0; // Изначально кодер в состоянии 0

    for (int step = 0; step < 5; step++) {
        unsigned char next_bit = tx_payload_bits[step];
        unsigned char target_state = true_states_history[step];

        // РЕАЛЬНОЕ ЧТЕНИЕ: Получаем упакованный байт из вашей таблицы
        unsigned char packed_dibit = out_bits[current_state_sim][next_bit];

        // РАСПАКОВКА: достаем o1 и o2 так, как они были упакованы при init
        unsigned char expected_out1 = (packed_dibit >> 1) & 1;
        unsigned char expected_out2 = packed_dibit & 1;

        // КРИТИЧЕСКИЙ ЧИХ: Печатаем, что реально лежит в ячейке памяти
         printf("[СЫРЫЕ ДАННЫЕ] step %d: значение в массиве = %d (0x%02X)\n",
                step, tx_payload_bits[step], tx_payload_bits[step]);

        printf("Шаг %d: Из состояния %d по биту %d переходим в %d.\n",
               step, current_state_sim, next_bit, target_state);

        printf("       Таблица out_bits ожидает на КВ-приеме дибит: [%d, %d]\n",
               expected_out1, expected_out2);

        // Внимание: на следующем шаге текущим становится целевое состояние
        current_state_sim = target_state;
    }
    printf("------------------------------------------------------\n\n");

    // Переменные для работы алгоритма (сопряжено с вашим conv_encoder.c)
    unsigned int metrics[64];
    unsigned int next_metrics[64];

    // Выделяем историю переходов. Буквально пишем индекс предка, как решили ранее!
    // 840 шагов, для каждого из 64 состояний помним, из какого предка (0..63) мы пришли.
    static unsigned char debug_path_history[840][64];

    // Инициализация метрик (0 для начального состояния, "бесконечность" для остальных)
    metrics[0] = 0;
    for (int i = 1; i < 64; i++) {
        metrics[i] = 999999;
    }

    // Основной цикл прямого хода по всем 840 символам пакета
    for (int step = 0; step < 840; step++) {
        // Извлекаем пару мягких бит из депунктуризатора для текущего шага
        unsigned char soft_r1 = in_soft_bits_1_2[step * 2];
        unsigned char soft_r2 = in_soft_bits_1_2[step * 2 + 1];

        if (step >= 720 && step <= 723) {
            // Узнаем, какие именно полиномы G1/G2 выдал кодер для истинного перехода
            unsigned char true_now = true_states_history[step];
            // Чтобы узнать предка, заглянем в историю на шаг назад (если step > 0)
            unsigned char true_prev = (step > 0) ? true_states_history[step - 1] : 0;
            // Определяем, какой информационный бит зашел
            unsigned char true_bit = tx_payload_bits[step];

            unsigned char packed_dibit = out_bits[true_prev][true_bit];
            unsigned char expected_g1 = ((packed_dibit >> 1) & 1) ? 255 : 0;
            unsigned char expected_g2 = (packed_dibit & 1) ? 255 : 0;

            printf("[ВХОД ДЕКОДЕРА] Шаг %d: Ожидаем от идеального кодера: G1=%d, G2=%d\n", step, expected_g1, expected_g2);
            printf("               Реально пришло из депунктуризатора: soft_r1=%d, soft_r2=%d\n", soft_r1, soft_r2);
        }

        // Готовим массив под новые метрики
        for (int i = 0; i < 64; i++) {
            next_metrics[i] = 999999;
        }

        // Перебор всех текущих состояний решетки (Branch Metric + ACS)
        for (int i = 0; i < 64; i++) {
            if (metrics[i] > 900000) continue; // Путь еще мертв

            // Из каждого состояния 'i' возможны два перехода по биту 'bit' (0 или 1)
            for (int bit = 0; bit < 2; bit++) {
                // Находим следующее состояние 'next' (биты сдвигаются влево)
                int next = ((i << 1) | bit) & 0x3F;

                // Читаем эталонный дибит для этого перехода из упакованной таблицы
                unsigned char packed_dibit = out_bits[i][bit];
                unsigned char o1 = ((packed_dibit >> 1) & 1) ? 255 : 0;
                unsigned char o2 = (packed_dibit & 1) ? 255 : 0;

                // Считаем Евклидово расстояние (L1-норма) для мягких бит
                unsigned int dist = 0;
                dist += (unsigned int)abs((int)soft_r1 - (int)o1);
                dist += (unsigned int)abs((int)soft_r2 - (int)o2);

                // Новая метрика для состояния 'next'
                unsigned int new_metric = metrics[i] + dist;

                // Выбираем лучший путь (минимальную метрику)
                if (new_metric < next_metrics[next]) {
                    next_metrics[next] = new_metric;
                    debug_path_history[step][next] = (unsigned char)i; // Запоминаем предка!
                }
            }
        }

        // Переносим рассчитанные метрики на следующий шаг
        for (int i = 0; i < 64; i++) {
            metrics[i] = next_metrics[i];
        }

        // ================= ШПИОН ЗАДАННОЙ ТРАЕКТОРИИ =================
        // Находим, в каком состоянии СЕЙЧАС должен быть кодер
        unsigned char true_state_now = true_states_history[step];
        unsigned int true_state_metric = metrics[true_state_now];

        // Ищем, какая вообще минимальная метрика сейчас есть в решетке среди живых
        unsigned int global_min_metric = 999999;
        for (int i = 0; i < 64; i++) {
            if (metrics[i] < global_min_metric) {
                global_min_metric = metrics[i];
            }
        }

        // Если истинный путь отвалился или его штраф стал аномально огромным
        if (true_state_metric > 900000) {
            printf("[КРИТИЧЕСКИЙ СБОЙ] Шаг %d: Истинный путь (состояние %d) был ОТБРОШЕН алгоритмом как мертвый!\n",
                   step, true_state_now);
            break; // Нет смысла идти дальше, решетка полностью разрушена
        }

        // Раз в 100 шагов (и обязательно на проблемном участке 720+) выводим отчет шпиона
        if (step % 100 == 0 || (step >= 720 && step <= 730)) {
            printf("[ШПИОН] Шаг %d: Истинное состояние кодера: %d (Метрика: %u). Лучшая метрика в решетке: %u\n",
                   step, true_state_now, true_state_metric, global_min_metric);
        }
        // =============================================================
    }
    // Очищаем выходной буфер (105 байт для 840 информационных бит)
    memset(out_bytes_p, 0, 105);

    // В соответствии с концепцией Zero-Tail, мы ТОЧНО знаем,
    // что кодер финишировал в состоянии 000000.
    int curr_state = 0;

    int traceback_errors = 0;

    printf("\n--- [ОТЛАДКА TRACEBACK] Запуск обратного хода от шага 839 ---\n");

    for (int step = 839; step >= 0; step--) {
        // 1. Извлекаем истинное состояние кодера на этом шаге для проверки
        unsigned char expected_state = true_states_history[step];

        // ДЕТЕКТОР РАЗРЫВА: Проверяем, совпадает ли траектория Витерби с истинной
        if (curr_state != expected_state && traceback_errors < 10) {
            printf("[РАЗРЫВ ТРАЕКТОРИИ] Шаг %d: Декодер находится в состоянии %d, а должен быть в %d!\n",
                   step, curr_state, expected_state);
            traceback_errors++;
        }

        // 2. Информационный бит — это всегда МЛАДШИЙ бит текущего состояния
        // (Так как при кодировании мы делали: (reg << 1) | bit)
        unsigned char bit = (unsigned char)(curr_state & 1);

        // 3. Упаковываем восстановленный бит в массив байт (MSB-first, от старшего к младшему)
        int byte_pos = step / 8;
        int bit_pos = 7 - (step % 8);
        out_bytes_p[byte_pos] |= (bit << bit_pos);

        // 4. МГНОВЕННЫЙ ПЕРЕХОД НАЗАД: читаем точный индекс предка из нашей истории
        curr_state = debug_path_history[step][curr_state];
    }

    printf("--- [КОНЕЦ TRACEBACK] Найдено точек расхождения траекторий: %d ---\n\n", traceback_errors);

} // Конец функции viterbi_debug
#endif

/* Принимает 1680 мягких бит (где выколотые — это 127) и выдает 840 восстановленных бит */
void viterbi_decode_soft_1_2(const unsigned char *in_soft_bits_1_2, unsigned char *out_bytes) {
    // Внутренние массивы метрик состояний (64 состояния)
    unsigned int metrics[64];
    unsigned int next_metrics[64];

    // БЫЛО:
    // Статическая история выживших путей: 840 шагов по 64 байта предков
    // Занимает 53 760 байт в ОЗУ (или на стеке, если стек позволяет)
    //static unsigned char path_history[840][64];

    // СТАЛО:
    static unsigned char path_history[840][8]; // сжатая таблица истории (сохраняется старший бит 5)

    // 1. Инициализация метрик: стартуем строго из нулевого состояния
    metrics[0] = 0;
    for (int i = 1; i < 64; i++) {
        metrics[i] = 999999; // "Бесконечность" для мертвых путей
    }

    // 2. ПРЯМОЙ ХОД (ACS — Add, Compare, Select)
    for (int step = 0; step < 840; step++) {
        // Извлекаем пару мягких отсчетов (0..255, 127 = стертый бит)
        unsigned char soft_r1 = in_soft_bits_1_2[step * 2];
        unsigned char soft_r2 = in_soft_bits_1_2[step * 2 + 1];

        // Готовим буфер под новые метрики
        for (int i = 0; i < 64; i++) {
            next_metrics[i] = 999999;
        }

        // Перебор всех текущих состояний
        for (int i = 0; i < 64; i++) {
            if (metrics[i] > 900000) continue; // Пропускаем неактивные траектории

            // Переход по информационному биту (0 или 1)
            for (int bit = 0; bit < 2; bit++) {
                // Вычисляем индекс следующего состояния (сдвиг влево)
                int next = ((i << 1) | bit) & 0x3F;

                // Извлекаем эталонный упакованный дибит из таблицы инициализации
                unsigned char packed_dibit = out_bits[i][bit];
                unsigned char o1 = ((packed_dibit >> 1) & 1) ? 255 : 0;
                unsigned char o2 = (packed_dibit & 1) ? 255 : 0;

                // Евклидово расстояние (L1-норма) с защитой знака при вычитании
                unsigned int dist = 0;
                dist += (unsigned int)abs((int)soft_r1 - (int)o1);
                dist += (unsigned int)abs((int)soft_r2 - (int)o2);

                unsigned int new_metric = metrics[i] + dist;

                // Выбор лучшего пути
                if (new_metric < next_metrics[next]) {
                    next_metrics[next] = new_metric;
                    // БЫЛО: сохранение БУКВАЛЬНОГО индекса предка 'i'
                    // path_history[step][next] = (unsigned char)i;
                    // СТАЛО: сохранение в битовые позиции
                    int byte_idx = next / 8; // Упаковываем 5-й бит предка 'i' в нужный байт
                    int bit_idx  = next % 8; // и нужную битовую позицию состояния 'next'

                    if ((i >> 5) & 1) {
                        path_history[step][byte_idx] |= (1 << bit_idx);
                    } else {
                        path_history[step][byte_idx] &= ~(1 << bit_idx);
                    }
                }
            }
        }

        // Перенос метрик на следующий шаг с одновременной нормализацией против переполнения
        unsigned int min_current = next_metrics[0];
        for (int i = 1; i < 64; i++) {
            if (next_metrics[i] < min_current) {
                min_current = next_metrics[i];
            }
        }
        for (int i = 0; i < 64; i++) {
            metrics[i] = (next_metrics[i] < 900000) ? (next_metrics[i] - min_current) : 999999;
        }
    }

    // 3. ОБРАТНЫЙ ХОД (TRACEBACK) — O(1) за шаг
    // Полная очистка выходного буфера на 105 байт (840 бит)
    memset(out_bytes, 0, 105);

    // Концепция Zero-Tail: кодер гарантированно финишировал в состоянии 0
    int curr_state = 0;

    for (int step = 839; step >= 0; step--) {
        // Восстанавливаемый информационный бит — это всегда младший бит текущего состояния
        unsigned char bit = (unsigned char)(curr_state & 1);

        // Упаковка бита в байт (MSB-first: от старшего бита в байте к младшему)
        int byte_pos = step / 8;
        int bit_pos = 7 - (step % 8);
        out_bytes[byte_pos] |= (bit << bit_pos);

        // Мгновенный прыжок к предку по таблице истории
        // БЫЛО: curr_state = path_history[step][curr_state];
        // СТАЛО: МАТЕМАТИЧЕСКОЕ ВОССТАНОВЛЕНИЕ ПРЕДКА O(1):
        // Извлекаем сохраненный старший бит предка
        int byte_idx = curr_state / 8;
        int bit_idx  = curr_state % 8;
        unsigned char decision = (path_history[step][byte_idx] >> bit_idx) & 1;

        // Собираем предка: сдвигаем текущее состояние вправо
        // и возвращаем сохраненный бит 'decision' на место 5-го бита (MSB)
        curr_state = ((curr_state >> 1) | (decision << 5)) & 0x3F;
    }
}
