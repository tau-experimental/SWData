#ifndef SCRAMBLER_H
#define SCRAMBLER_H

/* Начальное состояние LFSR (не должно быть нулем!) */
#define SCRAMBLER_SEED 0x7F

/* Сброс скремблера в начальное состояние перед началом нового блока */
void scrambler_reset(unsigned char *lfsr);

/* Обработка одного байта (работает в обе стороны: и скремблирование, и дескремблирование) */
/* lfsr: указатель на переменную состояния (текущее состояние регистра) */
/* data_byte: входной байт данных */
/* Возвращает: обработанный байт */
unsigned char scrambler_process_byte(unsigned char *lfsr, unsigned char data_byte);

#endif /* SCRAMBLER_H */
