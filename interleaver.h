#ifndef INTERLEAVER_H
#define INTERLEAVER_H

#include "reed_solomon.h"

/* Максимальное количество пакетов в одном блоке перемежения. Сейчас геометрия такая: 4 пакета по 26 байт (20 данных + 6 кодов Рида-Соломона */
#define INT_MAX_PACKETS 8

typedef struct {
    int packet_len;   /* Длина одного RS-пакета (например, 26) */
    int num_packets;  /* Сколько пакетов объединяем в блок (например, 4) */
    int total_bytes;  /* Общий размер блока (packet_len * num_packets) */
} int_config_t;

/* Инициализация конфигурации перемежителя */
void interleaver_init(int_config_t *cfg, int packet_len, int num_packets);

/* Перемежение (Tx сторона) */
/* in_blocks:  указатель на массив пакетов, лежащих подряд (размер: packet_len * num_packets) */
/* out_stream: буфер, куда запишется перемешанный поток для отправки в эфир */
void interleaver_process(const int_config_t *cfg, const unsigned char *in_blocks, unsigned char *out_stream);

/* Деперемежение (Rx сторона) */
/* in_stream:  перемешанный поток, принятый из эфира */
/* out_blocks: буфер, куда восстановятся исходные пакеты, разложенные по строкам */
void deinterleaver_process(const int_config_t *cfg, const unsigned char *in_stream, unsigned char *out_blocks);

#endif /* INTERLEAVER_H */
