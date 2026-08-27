#ifndef DDS_H
#define DDS_H

#include <stdint.h>
#include "dsp_utils.h"

#define DDS_TABLE_SIZE 256
#define DDS_TABLE_MASK (DDS_TABLE_SIZE - 1)

typedef struct {
    uint32_t phase_accumulator;
    uint32_t phase_step;
} dds_t;

extern float dds_sin_table[DDS_TABLE_SIZE];

void dsp_dds_init_table(void);
void dsp_dds_init(dds_t *dds, float frequency, float sample_rate);

/* Вычисляет комплексный сэмпл I/Q, сохраняет по указателю out и делает один шаг по фазе */
void dsp_dds_next_complex(dds_t *dds, float phase_offset_rad, complex_f *out);
void dsp_dds_set_frequency(dds_t *dds, float frequency, float sample_rate);

#endif /* DDS_H */
