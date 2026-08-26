#ifndef RX_H
#define RX_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// Состояния приемника
typedef enum {
    RX_STATE_SEARCH,      // Поиск преамбулы (автокорреляция)
    RX_STATE_GUARD,       // Ожидание/пропуск защитного интервала
    RX_STATE_DECODE       // Демодуляция символов данных
} rx_state_t;

void rx_init(void);

uint8_t rx_decode_symbol(complex_f *data_buffer, uint32_t absolute_symbol_idx);

#endif // RX_H
