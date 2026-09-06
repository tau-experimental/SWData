#include "interleaver.h"
#include <string.h>

void interleaver_init(int_config_t *cfg, int packet_len, int num_packets) {
    // Переводим геометрию на уровень БИТ
    cfg->packet_len = packet_len * 8;   // Было 26 байт -> стало 208 бит
    cfg->num_packets = num_packets;     // Количество пакетов (столбцов)
    // Общий размер буфера в БАЙТАХ для совместимости выделения памяти
    cfg->total_bytes = packet_len * num_packets;
}

/* Вспомогательные инлайны для чтения/записи конкретного бита в байтовом массиве */
static inline int get_bit_at(const unsigned char *buffer, int bit_idx) {
    // bit_idx >> 3 — это деление на 8 (находим байт)
    // 7 - (bit_idx & 7) — порядок бит от старшего к младшему (MSB-first)
    return (buffer[bit_idx >> 3] >> (7 - (bit_idx & 7))) & 1;
}

static inline void set_bit_at(unsigned char *buffer, int bit_idx, int bit_val) {
    if (bit_val) {
        buffer[bit_idx >> 3] |= (1 << (7 - (bit_idx & 7)));
    } else {
        buffer[bit_idx >> 3] &= ~(1 << (7 - (bit_idx & 7)));
    }
}

/* Запись по строкам (биты пакета), чтение по столбцам (биты разных пакетов) */
void interleaver_process(const int_config_t *cfg, const unsigned char *in_blocks, unsigned char *out_stream) {
    int r, c;
    int write_bit_ptr = 0;

    // Очищаем выходной буфер, так как будем выставлять биты поштучно
    memset(out_stream, 0, cfg->total_bytes);

    for (c = 0; c < cfg->packet_len; c++) {       // Идем по битам пакета (столбцы матрицы)
        for (r = 0; r < cfg->num_packets; r++) {   // Идем по пакетам (строки матрицы)
            /* Линейный индекс конкретного бита в исходном массиве пакетов */
            int in_bit_idx = r * cfg->packet_len + c;
            int bit_val = get_bit_at(in_blocks, in_bit_idx);

            /* Последовательно упаковываем биты в выходной поток */
            set_bit_at(out_stream, write_bit_ptr++, bit_val);
        }
    }
}

/* Запись по столбцам, чтение по строкам */
void deinterleaver_process(const int_config_t *cfg, const unsigned char *in_stream, unsigned char *out_blocks) {
    int r, c;
    int read_bit_ptr = 0;

    // Очищаем буфер восстановления пакетов
    memset(out_blocks, 0, cfg->total_bytes);

    for (c = 0; c < cfg->packet_len; c++) {
        for (r = 0; r < cfg->num_packets; r++) {
            /* Извлекаем бит из принятого последовательного потока */
            int bit_val = get_bit_at(in_stream, read_bit_ptr++);

            /* Вычисляем его исходную битовую координату */
            int out_bit_idx = r * cfg->packet_len + c;
            set_bit_at(out_blocks, out_bit_idx, bit_val);
        }
    }
}

#include <stdint.h>
#define INTRLVL_BYTES	(26*4)
void inerleaver_test(void) {
	int_config_t cfg;
	int i, ok = 1;
	uint8_t RawSource[INTRLVL_BYTES];
	uint8_t Intr[INTRLVL_BYTES];
	uint8_t Deintr[INTRLVL_BYTES];
	interleaver_init (&cfg, 26, 4); // и не забываем, что мы указываем именно количество байт в пакете
	for (i = 0; i < INTRLVL_BYTES; i++) {
		RawSource[i] = (uint8_t)rand();
	}
	interleaver_process (&cfg, RawSource, Intr);
	deinterleaver_process (&cfg, Intr, Deintr);
	for (i = 0; i < INTRLVL_BYTES; i++) {
		if (RawSource[i] != Deintr[i]) {
			printf ("ошибка (де)интерливера на байте $u: 0x%02X != 0x%02X!\n", i, RawSource[i], Deintr[i]);
			ok = 0;
		}
	}
	printf ("Тест интерливера: %s\n", ok ? "УСПЕХ": "ПРОВАЛ");
}
