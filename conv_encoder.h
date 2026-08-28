#ifndef CONV_ENCODER_H
#define CONV_ENCODER_H

/* Базовые полиномы для стандартного кодера K=7, Rate 1/2 (стандарт NASA) */
#define POLY_G1 0x6D /* 1101101 в двоичной системе */
#define POLY_G2 0x4F /* 1001111 в двоичной системе */

#define NUM_STATES 64 /* размер ершётки для Витерби */

/* Структура состояния сверточного кодера */
typedef struct {
    unsigned char reg; /* 7-битный сдвиговый регистр памяти кодера */
} conv_encoder_t;

/* Сброс памяти кодера перед началом нового блока (очистка регистра в 0) */
void conv_encoder_reset(conv_encoder_t *enc);

/* Кодирование блока данных */
/* enc:        указатель на состояние */
/* in_bytes:   входной поток после скремблера (104 байта) */
/* in_len:     длина входного потока (104) */
/* out_dibits: выходной массив дибитов для модулятора.                      */
/*             Размер: 104 байта * 8 бит = 832 бита данных.                  */
/*             Код Rate 5/6 превратит их в (832 * 6 / 5) = 1000 бит в эфире. */
/*             1000 бит / 2 бита на символ = 500 дибитов (символов).         */
void conv_encode_block(conv_encoder_t *enc, const unsigned char *in_bytes, int in_len, unsigned char *out_dibits);

/* Декодирование блока Витерби (Rx) */
/* out_bytes: буфер для восстановленных 105 байт */
/* in_dibits: массив из 504 дибитов, принятых из канала */
void viterbi_decode_block(unsigned char *out_bytes, const unsigned char *in_dibits);

void viterbi_decode_soft_1_2(const unsigned char *in_soft_bits_1_2, unsigned char *out_bits);
void conv_encode_pure_1_2(conv_encoder_t *enc, const unsigned char *in_bits, unsigned char *out_bits_1_2);

#endif /* CONV_ENCODER_H */
