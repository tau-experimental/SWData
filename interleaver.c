#include "interleaver.h"
#include <string.h>

void interleaver_init(int_config_t *cfg, int packet_len, int num_packets) {
    cfg->packet_len = packet_len;
    cfg->num_packets = num_packets;
    cfg->total_bytes = packet_len * num_packets;
}

/* Запись по строкам (пакетам), чтение по столбцам */
void interleaver_process(const int_config_t *cfg, const unsigned char *in_blocks, unsigned char *out_stream) {
    int r, c;
    int write_ptr = 0;

    for (c = 0; c < cfg->packet_len; c++) {
        for (r = 0; r < cfg->num_packets; r++) {
            /* Индекс элемента в исходной матрице [r][c] */
            int in_idx = r * cfg->packet_len + c;
            out_stream[write_ptr++] = in_blocks[in_idx];
        }
    }
}

/* Запись по столбцам, чтение по строкам (пакетам) */
void deinterleaver_process(const int_config_t *cfg, const unsigned char *in_stream, unsigned char *out_blocks) {
    int r, c;
    int read_ptr = 0;

    for (c = 0; c < cfg->packet_len; c++) {
        for (r = 0; r < cfg->num_packets; r++) {
            int out_idx = r * cfg->packet_len + c;
            out_blocks[out_idx] = in_stream[read_ptr++];
        }
    }
}
