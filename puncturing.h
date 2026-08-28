#ifndef PUNCTURING_H
#define PUNCTURING_H

/* Выкалывание: 1680 бит (Rate 1/2) -> 1008 бит (Rate 5/6) */
void apply_puncturing(const unsigned char *in_bits_1_2, unsigned char *out_bits_5_6);

/* Депунктурирование: 1008 бит (Rate 5/6) -> 1680 бит мягких решений */
/* Выколотые биты заполняются нейтральным значением 127 */
/* Выжившие биты: 0 превращается в 0, 1 превращается в 255 */
void apply_depuncturing(const unsigned char *in_bits_5_6, unsigned char *out_soft_bits_1_2);

#endif /* PUNCTURING_H */
