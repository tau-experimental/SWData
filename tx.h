#ifndef TX_H
#define TX_H

#include <stdint.h>

// Состояния модулятора
typedef enum {
    TX_STATE_IDLE,
    TX_STATE_PREAMBLE,
    TX_STATE_DATA
} tx_state_t;

// Инициализация передатчика
void tx_init(void);

// Потоковый генератор: возвращает один сэмпл за один вызов
// бит `data_bit` используется только в состоянии TX_STATE_DATA
int16_t tx_get_next_sample(tx_state_t state, uint8_t data_bit);

#endif // TX_H
