#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>
#include "dsp_utils.h"
#include "complex_filter.h"

typedef struct {
    uint8_t current_nibble;      // Текущий декодированный ниббл (4 бита)
    uint8_t received_byte;       // Собранный из двух нибблов байт
    int nibble_toggle;           // Триггер: 0 - ждем верхний ниббл, 1 - ждем нижний
} dsp_decoder_t;

void dsp_decoder_init(dsp_decoder_t *dec);

/* Вызывается СТРОГО в момент strobe == true.
   Выполняет групповое выравнивание по пилот-тону 1300 Гц,
   определяет фазовые сектора, собирает нибблы и байты.
   Возвращает 1, если байт полностью готов, иначе 0. */
int dsp_decoder_process_strobe(dsp_decoder_t *dec, const complex_f *diff_outputs);

#endif /* DECODER_H */
