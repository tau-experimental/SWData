#ifndef TX_H
#define TX_H

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

// Инициализация DDS таблиц и аккумуляторов фаз
void tx_init(void);

// Сдвиг фаз (+90 или -90) на границе символа на основе ниббла
void tx_step_phase(uint8_t nibble);

// Потоковый генератор: возвращает один комплексный сэмпл за вызов
void tx_get_sample(complex_f *out);

#endif // TX_H
