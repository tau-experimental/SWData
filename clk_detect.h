#ifndef CLK_DETECT_H_
#define CLK_DETECT_H_

#include "complex_math.h"

#define SYM_LEN 800
#define HALF_SYM 400

typedef struct {
    cplx_f32 delay_line[SYM_LEN]; // Единое FIFO на 800 сэмплов (3.2 КБ)
    cplx_f32 running_sum;         // Комплексный аккумулятор
    int ptr;                      // Единый указатель записи головы
} clk_detect_t;

#endif /* CLK_DETECT_H_ */
