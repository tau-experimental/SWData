#ifndef CONV_LLR_ENCODER_H
#define CONV_LLR_ENCODER_H

#include <stdint.h>

/* Базовые полиномы для стандартного кодера K=7, Rate 1/2 (стандарт NASA) */
#define NEW_POLY_G1 0x5B // Standard NASA (01011011b)
#define NEW_POLY_G2 0x7F // Standard NASA (01111111b)

#define NUM_STATES 64 /* размер решётки для Витерби */

#define SHORT_STEPS 118 // 112 бит данных + 6 бит хвоста
#define INF_METRIC  0x3FFFFFFF // Замена 999999 на "бесконечность", удобную для процессора


/* Таблица переходов для ускорения декодера (генерируется при инициализации) */
/* out_bits[состояние][входной_бит] содержит два гипотетических жестких бита (0 или 1), */
/* которые должен был выдать кодер при таком переходе. */
extern uint8_t out_bits[NUM_STATES][2];
//extern uint8_t out_bits[];

/* Структура состояния сверточного кодера */
typedef struct {
	uint8_t reg; /* 7-битный сдвиговый регистр памяти кодера */
} conv_llr_ack_encoder_t;

/* МЯГКОЕ ДЕКОДИРОВАНИЕ ВИТЕРБИ (LLR) */
/* in_soft_bits_236: входной массив из 236 значений int8_t LLR, полученный из деперемежителя */
/*                   (+127 = уверенный 0, -127 = уверенная 1, 0 = затухание/стирание) */
/* out_decoded_bits: выходной массив из 112 восстановленных жестких бит для КРС (0 или 1) */
void viterbi_llr_decode_soft_1_2_ack(const int8_t *in_soft_bits_236, uint8_t *out_decoded_bits);


/* Кодирование "чистого" блока данных R=1/2 с добавлением хвоста (Tail Bits) */
/* in_bits:  входной поток от КРС (112 бит, каждый бит в своем байте 0 или 1 для простоты кодера) */
/* out_bits_1_2: выходной поток жестких бит (размер 112 + 6 хвост = 118 бит * 2 = 236 бит) */
void conv_llr_encode_pure_1_2_ack(conv_llr_ack_encoder_t *enc, const uint8_t *in_bits, uint8_t *out_bits_1_2);

/* "компилятор" таблицы для flash */
void viterbi_table_gen(void);

#endif /* CONV_LLR_ENCODER_H */
