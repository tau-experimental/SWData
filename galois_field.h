#ifndef GALOIS_FIELD_H
#define GALOIS_FIELD_H

extern unsigned char gf_exp[];

/* Инициализация таблиц GF(256). Вызывается один раз при старте программы. */
/* На ПК мы сгенерируем её динамически, а для чипа потом просто зашьем как const */
void gf_init(void);

/* Сложение в поле Галуа (просто XOR) */
unsigned char gf_add(unsigned char a, unsigned char b);

/* Вычитание в поле Галуа (тоже просто XOR) */
unsigned char gf_sub(unsigned char a, unsigned char b);

/* Умножение двух байт по правилам Галуа через таблицы */
unsigned char gf_mul(unsigned char a, unsigned char b);

/* Деление байта 'a' на байт 'b' по правилам Галуа */
unsigned char gf_div(unsigned char a, unsigned char b);

#endif /* GALOIS_FIELD_H */
