#ifndef MODULATOR_H
#define MODULATOR_H

#include <stdint.h>
#include "dsp_utils.h"
#include "dds.h"
#include "complex_filter.h"

typedef struct {
    dds_t tone_gen[NUM_TONES];       // 4 генератора DDS для тонов
    float accumulated_phases[NUM_TONES]; // Текущая фаза манипуляции для каждого тона (в радианах)
    uint32_t symbol_duration;        // Длительность символа в сэмплах (например, 160)
    uint32_t sample_counter;         // Счетчик сэмплов внутри текущего символа
} modulator_t;

/* Инициализация модулятора (задание частот и базовой скорости) */
void dsp_modulator_init(modulator_t *mod, float sample_rate, uint32_t symbol_duration);

/* Посэмплный шаг модулятора.
   Принимает текущий ниббл для передачи.
   Возвращает комплексный сэмпл группового сигнала. */
void dsp_modulator_step(modulator_t *mod, uint8_t nibble, complex_f *out_sample);

extern float base_frequencies[];

#endif /* MODULATOR_H */
