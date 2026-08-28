#include "scrambler.h"

void scrambler_reset(unsigned char *lfsr) {
    *lfsr = SCRAMBLER_SEED;
}

/* Реализация PRBS-7 на базе полинома x^7 + x^6 + 1 */
unsigned char scrambler_process_byte(unsigned char *lfsr, unsigned char data_byte) {
    unsigned char out_byte = 0;
    unsigned char reg = *lfsr;
    int bit;

    /* Генерируем 8 псевдослучайных бит для XOR-а с входным байтом */
    for (bit = 0; bit < 8; bit++) {
        /* Извлекаем старший (выходящий) бит регистра как бит псевдослучайного шума */
        unsigned char noise_bit = (reg >> 6) & 1;

        /* Считаем обратную связь по полиному: x^7 + x^6 + 1 (в индексах 0-6 это биты 6 и 5) */
        unsigned char feedback = ((reg >> 6) ^ (reg >> 5)) & 1;

        /* Сдвигаем регистр влево и подмешиваем обратную связь в младший бит */
        reg = ((reg << 1) | feedback) & 0x7F; /* Сохраняем строго 7 бит */

        /* Собираем выходной байт шума */
        out_byte |= (noise_bit << bit);
    }

    *lfsr = reg; /* Сохраняем обновленное состояние для следующего байта */

    /* XOR входных данных с полученным байтом псевдослучайного шума */
    return data_byte ^ out_byte;
}
