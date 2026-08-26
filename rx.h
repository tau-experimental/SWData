#ifndef RX_H
#define RX_H

#include <stdint.h>
#include <stdbool.h>

// Состояния приемника
typedef enum {
    RX_STATE_SEARCH,      // Поиск преамбулы (автокорреляция)
    RX_STATE_GUARD,       // Ожидание защитного интервала
    RX_STATE_DECODE       // Демодуляция символов данных
} rx_state_t;

void rx_init(void);

// Потоковый обработчик: принимает один сэмпл из АЦП
// Возвращает true, если в этот момент был успешно декодирован бит данных
// потоковый алгоритм Шмидла-Кокса
bool rx_process_sample(int16_t sample, uint8_t *out_bit);

// Получить текущую оценку сдвига частоты в Гц (доступно после захвата преамбулы)
float rx_get_frequency_offset(void);

#endif // RX_H
