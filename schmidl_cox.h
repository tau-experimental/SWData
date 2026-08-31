#ifndef ___SCHMIDL_COX_SYNC___
#define ___SCHMIDL_COX_SYNC___

#define SC_SYM_LEN 800
#define SC_HALF_LEN 400

#include "complex_math.h"

typedef struct {
    cplx_f32 delay_line[SC_SYM_LEN];  // Единое FIFO на 800 комплексных сэмплов
    cplx_f32 running_sum;             // CIC-аккумулятор автокорреляции
    float energy_b;                   // Накопленная энергия правого окна (для нормировки)
    int ptr;                          // Указатель на текущую "Голову"
} schmidl_cox_t;

void sc_init(schmidl_cox_t *sc);
void sc_tick(schmidl_cox_t *sc, const cplx_f32 *input_sample, float *out_power, cplx_f32 *out_complex_corr);

#endif
