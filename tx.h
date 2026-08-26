#ifndef TX_H
#define TX_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// Состояния модулятора
typedef enum {
    TX_STATE_IDLE,
    TX_STATE_PREAMBLE,
    TX_STATE_DATA
} tx_state_t;

// Инициализация передатчика
void tx_init(void);
void tx_init_payload(void);

void tx_set_data_nibble(uint8_t nibble);

void tx_get_next_iq_sample(tx_state_t state, complex_f *out);

#endif // TX_H
