#ifndef RX_H
#define RX_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// Состояния приемника
typedef enum {
    RX_STATE_SEARCH,      // Поиск преамбулы (автокорреляция)
	RX_STATE_PREAMBLE_CALIBRATE,
    RX_STATE_DECODE       // Демодуляция символов данных
} rx_state_t;

#define SLIDING_WIN_LEN 128 // Выросло до 128 для когерентной устойчивости
#define SEARCH_WIN_LEN 160  // Возвращаем ультра-короткое окно для нечувствительности к КВ-дрейфу!


extern float dds_sine_table[];

void rx_init(void);

uint8_t rx_decode_symbol(complex_f *data_buffer, uint32_t absolute_symbol_idx);

extern float dpll_error_accumulator;

#endif // RX_H
