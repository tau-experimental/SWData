#include "puncturing.h"

/* Матрица выкалывания Rate 5/6 имеет период 5 входных бит (10 выходных бит скорости 1/2) */
/* Из каждых 10 бит мы оставляем только 6. Индексы выживших бит в блоке из 10: */
/* Паттерн: [0, 1, 2, 4, 6, 8] -> Выколоты индексы: 3, 5, 7, 9 */
void apply_puncturing(const unsigned char *in_bits_1_2, unsigned char *out_bits_5_6) {
    int i, p_idx = 0;

    /* Идем по всему массиву Rate 1/2 (1680 бит) блоками по 10 бит */
    for (i = 0; i < 1680; i += 10) {
        out_bits_5_6[p_idx++] = in_bits_1_2[i + 0]; /* g1 бита 0 */
        out_bits_5_6[p_idx++] = in_bits_1_2[i + 1]; /* g2 бита 0 */
        out_bits_5_6[p_idx++] = in_bits_1_2[i + 2]; /* g1 бита 1 */
        /* i+3 (g2 бита 1) — ВЫКОЛОТ */
        out_bits_5_6[p_idx++] = in_bits_1_2[i + 4]; /* g1 бита 2 */
        /* i+5 (g2 бита 2) — ВЫКОЛОТ */
        out_bits_5_6[p_idx++] = in_bits_1_2[i + 6]; /* g1 бита 3 */
        /* i+7 (g2 бита 3) — ВЫКОЛОТ */
        out_bits_5_6[p_idx++] = in_bits_1_2[i + 8]; /* g1 бита 4 */
        /* i+9 (g2 бита 4) — ВЫКОЛОТ */
    }
}

void apply_depuncturing(const unsigned char *in_bits_5_6, unsigned char *out_soft_bits_1_2) {
    int i, p_idx = 0;

    /* Восстанавливаем 1680 мягких отсчетов */
    for (i = 0; i < 1680; i += 10) {
        /* Известные биты переводим в жесткие метрики: 0 -> 0, 1 -> 255 */
        out_soft_bits_1_2[i + 0] = in_bits_5_6[p_idx++] ? 255 : 0;
        out_soft_bits_1_2[i + 1] = in_bits_5_6[p_idx++] ? 255 : 0;
        out_soft_bits_1_2[i + 2] = in_bits_5_6[p_idx++] ? 255 : 0;

        /* Выколотый бит! Заполняем строго нейтральным значением (середина диапазона) */
        out_soft_bits_1_2[i + 3] = 127;

        out_soft_bits_1_2[i + 4] = in_bits_5_6[p_idx++] ? 255 : 0;
        out_soft_bits_1_2[i + 5] = 127;

        out_soft_bits_1_2[i + 6] = in_bits_5_6[p_idx++] ? 255 : 0;
        out_soft_bits_1_2[i + 7] = 127;

        out_soft_bits_1_2[i + 8] = in_bits_5_6[p_idx++] ? 255 : 0;
        out_soft_bits_1_2[i + 9] = 127;
    }
}
