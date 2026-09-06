#ifndef REED_SOLOMON_H
#define REED_SOLOMON_H

#include <stdint.h>

/* Максимально возможные размеры для статического выделения памяти в структурах */
#define RS_MAX_N   255
#define RS_MAX_2T  32

/* Структура конфигурации геометрии Рида-Соломона */
typedef struct {
	uint8_t n;    /* Общая длина пакета в байтах (данные + защита) */
	uint8_t k;    /* Длина полезных данных в байтах */
	uint8_t t2;   /* Количество проверочных байт (n - k) */
    //unsigned char gen_poly[RS_MAX_2T + 1]; /* Генераторный полином для этой геометрии */
	const uint8_t *gen_poly; /* специфичная таблица в ПЗУ */
} rs_config_t;

/* Инициализация конкретной геометрии кода */
/* Пример использования: rs_init_geometry(&my_config, 26, 20); // 20 данных, 6 защиты */
void rs_init_geometry(rs_config_t *cfg, int n, int k);

/* Кодирование пакета (Tx) */
/* msg_in:     указатель на массив из cfg->k байт */
/* parity_out: указатель на массив из cfg->t2 байт, куда запишется защита */
void rs_encode_flexible(const rs_config_t *cfg, const unsigned char *msg_in, unsigned char *parity_out);

/* Декодирование пакета (Rx) */
/* packet_inout: указатель на массив из cfg->n байт (данные + защита) */
/*               Функция исправляет ошибки прямо внутри этого массива! */
/* Возвращает: количество исправленных байт (0 ... t), или -1 если пакет разрушен */
int rs_decode_flexible(const rs_config_t *cfg, unsigned char *packet_inout);

/* --- ОБЪЯВЛЕНИЕ КОНФИГУРАЦИЙ ВО FLASH --- */
/* Эти структуры настраиваются на этапе компиляции и не занимают ни байта ОЗУ! */
extern const rs_config_t rs_config_ack_14_8;   /* Для коротких ACK-пакетов */
extern const rs_config_t rs_config_data_26_20;  /* Для будущих больших DATA-пакетов */


/* Кодирование пакета (Tx) */
/* msg_in:     указатель на массив из cfg->k байт */
/* parity_out: указатель на массив из cfg->t2 байт, куда запишется защита */
void rs_encode_fast(const rs_config_t *cfg, const uint8_t *msg_in, uint8_t *parity_out);

/* Декодирование пакета (Rx) */
/* packet_inout: указатель на массив из cfg->n байт (данные + защита) */
/* Возвращает: количество исправленных байт (0 ... t), или -1 если пакет разрушен */
int rs_decode_fast(const rs_config_t *cfg, uint8_t *packet_inout);

#endif /* REED_SOLOMON_H */
