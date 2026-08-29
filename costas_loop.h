#ifndef __COSTAS_MEGA_LOOP__
#define __COSTAS_MEGA_LOOP__

#include "complex_math.h"

typedef struct {
    unsigned short phase_acc;      // 16-битный аккумулятор фазы локального DDS
    unsigned short phase_step_inc; // Базовый шаг частоты удержания (для 1000 Гц = 8192)
    short          *sine_lut;      // Указатель на таблицу синуса (256 элементов)

    float kp;                      // Пропорциональный коэффициент удержания
    float ki;                      // Интегральный коэффициент удержания
    float freq_integrator;         // Интегратор частоты (компенсирует дрейф)
} costas_loop_t;

void costas_loop_init(costas_loop_t *costas, short *sine_table_ptr);
void costas_loop_tick(costas_loop_t *costas, const cplx_f32 *in_sample, cplx_f32 *out_sample);
void costas_loop_gear_shift(costas_loop_t *costas);

#endif
