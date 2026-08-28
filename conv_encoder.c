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

void conv_encode_block(conv_encoder_t *enc, const unsigned char *in_bytes, int in_len, unsigned char *out_dibits) {
    int i, bit_idx;
    int punct_cycle = 0;
    int dibit_write_ptr = 0;
    unsigned char current_dibit = 0;
    int dibit_bit_cnt = 0;

    for (i = 0; i < in_len; i++) {
        unsigned char byte = in_bytes[i];
        for (bit_idx = 7; bit_idx >= 0; bit_idx--) {
            unsigned char input_bit = (byte >> bit_idx) & 1;
            enc->reg = ((enc->reg << 1) | input_bit) & 0x7F;

            unsigned char g1 = 0, g2 = 0;
            int j;
            for (j = 0; j < 7; j++) {
                if ((POLY_G1 >> j) & 1) g1 ^= (enc->reg >> j) & 1;
                if ((POLY_G2 >> j) & 1) g2 ^= (enc->reg >> j) & 1;
            }

            /* Вспомогательный макрос-лямбда для линейной упаковки бит в дибиты */
            #define PACK_BIT(b) do { \
                current_dibit = (current_dibit << 1) | (b); \
                if (++dibit_bit_cnt == 2) { \
                    out_dibits[dibit_write_ptr++] = current_dibit; \
                    current_dibit = 0; dibit_bit_cnt = 0; \
                } \
            } while(0)

            switch (punct_cycle) {
                case 0: PACK_BIT(g1); PACK_BIT(g2); break;
                case 1: PACK_BIT(g1); break;
                case 2: PACK_BIT(g2); break;
                case 3: PACK_BIT(g1); break;
                case 4: PACK_BIT(g2); break;
            }
            if (++punct_cycle == 5) punct_cycle = 0;
        }
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

        int needed = (punct_cycle == 0) ? 2 : 1;
        unsigned char got_bits[2] = {0, 0};

        for (i = 0; i < needed; i++) {
            if (dibit_bit_cnt == 0) {
                current_dibit = in_dibits[dibit_read_ptr++];
                dibit_bit_cnt = 2;
            }
            /* Читаем строго от старшего бита дибита к младшему (Линейно!) */
            got_bits[i] = (current_dibit >> (dibit_bit_cnt - 1)) & 1;
            dibit_bit_cnt--;
        }

        switch (punct_cycle) {
            case 0: r1 = got_bits[0]; r2 = got_bits[1]; break;
            case 1: r1 = got_bits[0]; mask2 = 0; break;
            case 2: r2 = got_bits[0]; mask1 = 0; break;
            case 3: r1 = got_bits[0]; mask2 = 0; break;
            case 4: r2 = got_bits[0]; mask1 = 0; break;
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
