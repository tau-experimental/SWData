#include "conv_encoder.h"
#include <string.h>
#include <stdio.h>

void conv_encoder_reset(conv_encoder_t *enc) {
    enc->reg = 0;
}
static int viterbi_inited = 0;
static unsigned char next_state[NUM_STATES][2];
static unsigned char out_bits[NUM_STATES][2];
static unsigned char prev_state[NUM_STATES][2];

static void viterbi_init_tables(void) {
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
        enc->reg = ((enc->reg << 1) | in_bits[i]) & 0x3F; /* 6 бит памяти */

        /* Полный 7-битный регистр для полиномов */
        unsigned char reg7 = (unsigned char)((enc->reg) | (in_bits[i] << 6));

        unsigned char g1 = 0, g2 = 0;
        for (j = 0; j < 7; j++) {
            if ((POLY_G1 >> j) & 1) g1 ^= (reg7 >> j) & 1;
            if ((POLY_G2 >> j) & 1) g2 ^= (reg7 >> j) & 1;
        }
        out_bits_1_2[write_ptr++] = g1;
        out_bits_1_2[write_ptr++] = g2;
    }
}

void viterbi_decode_block(unsigned char *out_bytes, const unsigned char *in_dibits) {
    static unsigned int metrics[NUM_STATES];
    static unsigned int next_metrics[NUM_STATES];

    /* Решетка путей: 840 шагов по 64 состояния (выделено статически) */
    static unsigned char path_history[840][NUM_STATES];

    int i, step, bit_idx;
    int punct_cycle = 0;

    /* Кристально чистый линейный экстрактор бит из потока дибитов */
    int dibit_read_ptr = 0;
    int dibit_bit_cnt = 0;
    unsigned char current_dibit = 0;

    viterbi_init_tables();

    for (i = 0; i < NUM_STATES; i++) metrics[i] = 999999;
    metrics[0] = 0;

    /* ПРЯМОЙ ХОД АЛГОРИТМА */
    for (step = 0; step < 840; step++) {
        unsigned char r1 = 0, r2 = 0;
        unsigned char mask1 = 1, mask2 = 1;

        /* Шаг 0: Переданы оба бита (g1, затем g2) */
        if (punct_cycle == 0) {
            /* Читаем g1 */
            if (dibit_bit_cnt == 0) { current_dibit = in_dibits[dibit_read_ptr++]; dibit_bit_cnt = 2; }
            r1 = (current_dibit >> (dibit_bit_cnt - 1)) & 1; dibit_bit_cnt--;

            /* Читаем g2 */
            if (dibit_bit_cnt == 0) { current_dibit = in_dibits[dibit_read_ptr++]; dibit_bit_cnt = 2; }
            r2 = (current_dibit >> (dibit_bit_cnt - 1)) & 1; dibit_bit_cnt--;
        }
        /* Шаг 1: Передан только g1 (g2 выколот) */
        else if (punct_cycle == 1) {
            if (dibit_bit_cnt == 0) { current_dibit = in_dibits[dibit_read_ptr++]; dibit_bit_cnt = 2; }
            r1 = (current_dibit >> (dibit_bit_cnt - 1)) & 1; dibit_bit_cnt--;
            mask2 = 0; /* Нейтральное значение для выколотого бита */
        }
        /* Шаг 2: Передан только g2 (g1 выколот) */
        else if (punct_cycle == 2) {
            if (dibit_bit_cnt == 0) { current_dibit = in_dibits[dibit_read_ptr++]; dibit_bit_cnt = 2; }
            r2 = (current_dibit >> (dibit_bit_cnt - 1)) & 1; dibit_bit_cnt--;
            mask1 = 0;
        }
        /* Шаг 3: Передан только g1 (g2 выколот) */
        else if (punct_cycle == 3) {
            if (dibit_bit_cnt == 0) { current_dibit = in_dibits[dibit_read_ptr++]; dibit_bit_cnt = 2; }
            r1 = (current_dibit >> (dibit_bit_cnt - 1)) & 1; dibit_bit_cnt--;
            mask2 = 0;
        }
        /* Шаг 4: Передан только g2 (g1 выколот) */
        else if (punct_cycle == 4) {
            if (dibit_bit_cnt == 0) { current_dibit = in_dibits[dibit_read_ptr++]; dibit_bit_cnt = 2; }
            r2 = (current_dibit >> (dibit_bit_cnt - 1)) & 1; dibit_bit_cnt--;
            mask1 = 0;
        }

        punct_cycle = (punct_cycle + 1) % 5;

        for (i = 0; i < NUM_STATES; i++) next_metrics[i] = 999999;

        for (i = 0; i < NUM_STATES; i++) {
            if (metrics[i] > 900000) continue;

            for (bit_idx = 0; bit_idx < 2; bit_idx++) {
                int next = next_state[i][bit_idx];
                unsigned char out = out_bits[i][bit_idx];
                unsigned char o1 = (out >> 1) & 1;
                unsigned char o2 = out & 1;

                unsigned int dist = 0;
                if (mask1) dist += (r1 ^ o1);
                if (mask2) dist += (r2 ^ o2);

                unsigned int new_metric = metrics[i] + dist;
                if (new_metric < next_metrics[next]) {
					next_metrics[next] = new_metric;
                    /* ИСПРАВЛЕНО: Сохраняем СТАРШИЙ бит состояния-предка. */
                    /* Это однозначно фиксирует, из какой половины решетки пришел выживший путь! */
                    path_history[step][next] = (unsigned char)((i >> 5) & 1);
				}
            }
        }
        memcpy(metrics, next_metrics, sizeof(metrics));
    }

    printf("[ОТЛАДКА VITERBI] Финальная метрика нулевого состояния: %u\n", metrics[0]);

    /* ОБРАТНЫЙ ХОД (TRACEBACK) с поиском лучшего финишного состояния */
    int curr_state = 0;
    unsigned int min_metric = 999999;

    /* Ищем истинное финишное состояние с минимальной ошибкой */
    for (i = 0; i < NUM_STATES; i++) {
        if (metrics[i] < min_metric) {
            min_metric = metrics[i];
            curr_state = i;
        }
    }

    printf("[ОТЛАДКА VITERBI] Обратный ход запущен из состояния %d (Метрика: %u)\n", curr_state, min_metric);

    static unsigned char decoded_bits[840];

    for (step = 839; step >= 0; step--) {
        /* Информационный бит — это ВСЕГДА младший бит текущего состояния в решетке, */
        /* так как кодер задвигал биты справа налево! */
        decoded_bits[step] = (unsigned char)(curr_state & 1);

        /* Читаем бит решения (какой предок выжил — из верхней или нижней половины) */
        unsigned char decision = path_history[step][curr_state];

        /* Восстанавливаем точное предыдущее состояние за 1 такт процессора: */
        /* Сдвигаем текущее состояние вправо и возвращаем выживший старший бит на место */
        curr_state = ((curr_state >> 1) | (decision << 5)) & 0x3F;
    }

    /* Упаковываем восстановленные биты обратно в 105 байт */
    memset(out_bytes, 0, 105);
    for (step = 0; step < 840; step++) {
        int byte_pos = step / 8;
        int bit_pos = 7 - (step % 8);
        if (decoded_bits[step]) {
            out_bytes[byte_pos] |= (1 << bit_pos);
        }
    }
}

/* Принимает 1680 мягких бит (где выколотые — это 127) и выдает 840 восстановленных бит */
void viterbi_decode_soft_1_2(const unsigned char *in_soft_bits_1_2, unsigned char *out_bits) {
    static unsigned int metrics[NUM_STATES];
    static unsigned int next_metrics[NUM_STATES];
    static unsigned char path_history[840][NUM_STATES];

    int i, step, bit_idx;
    viterbi_init_tables(); /* Таблицы next_state теперь генерируются строго под Rate 1/2 */

    for (i = 0; i < NUM_STATES; i++) metrics[i] = 999999;
    metrics[0] = 0;

    int read_ptr = 0;
    for (step = 0; step < 840; step++) {
        /* Берем два мягких отсчета из эфира */
        unsigned char soft_r1 = in_soft_bits_1_2[read_ptr++];
        unsigned char soft_r2 = in_soft_bits_1_2[read_ptr++];

        for (i = 0; i < NUM_STATES; i++) next_metrics[i] = 999999;

        for (i = 0; i < NUM_STATES; i++) {
            if (metrics[i] > 900000) continue;

            for (bit_idx = 0; bit_idx < 2; bit_idx++) {
                int next = next_state[i][bit_idx];
                unsigned char** out_bits_p = (unsigned char**)out_bits;
                unsigned char out = out_bits_p[i][bit_idx];

                /* Идеальные целевые значения для этого перехода: 0 или 255 */
                unsigned char target_o1 = ((out >> 1) & 1) ? 255 : 0;
                unsigned char target_o2 = (out & 1) ? 255 : 0;

                /* Мягкая метрика — Евклидово расстояние (abs разность) */
                unsigned int dist = 0;
                dist += (unsigned int)(char)abs(soft_r1 - target_o1);
                dist += (unsigned int)(char)abs(soft_r2 - target_o2);

                unsigned int new_metric = metrics[i] + dist;
                if (new_metric < next_metrics[next]) {
                    next_metrics[next] = new_metric;
                    path_history[step][next] = (unsigned char)bit_idx; /* Сохраняем выживший информационный бит! */
                }
            }
        }
        memcpy(metrics, next_metrics, sizeof(metrics));
    }

    /* TRACEBACK (Обратный ход по решетке) */
    int curr_state = 0;
    unsigned int min_metric = 999999;
    for (i = 0; i < NUM_STATES; i++) {
        if (metrics[i] < min_metric) { min_metric = metrics[i]; curr_state = i; }
    }

    for (step = 839; step >= 0; step--) {
        unsigned char bit = path_history[step][curr_state];
        out_bits[step] = bit;

        /* Восстановление предка для Rate 1/2 через наш канонический сдвиг */
        /* Поиск предка */
        int prev_state = 0;
        for (i = 0; i < NUM_STATES; i++) {
            if (next_state[i][bit] == curr_state) { prev_state = i; break; }
        }
        curr_state = prev_state;
    }
}

