#ifndef ___MLS31_SYNCHRONIZATOR__
#define ___MLS31_SYNCHRONIZATOR__

#define MLS_LEN 31
#define SAMPLES_PER_SYMBOL 800
#define SC_HALF_LEN        (SAMPLES_PER_SYMBOL/2)
#define DECIMATION_FACTOR  100
#define POINTS_PER_SYMBOL  (SAMPLES_PER_SYMBOL / DECIMATION_FACTOR) // 8 точек на символ
#define MLS_MACRO_HISTORY_LEN (MLS_LEN * POINTS_PER_SYMBOL)         // 248 точек
#define DECLK_WINDOW 200 // для детектора синхропустышки

#include <stdint.h>
#include "complex_math.h"

typedef struct {
    // === СТУПЕНЬ 1: Буферы и переменные Шмидля-Кокса ===
    cplx_f32 delay_line[SAMPLES_PER_SYMBOL];  // Монолитное FIFO на 800 сэмплов (3.2 КБ)
    cplx_f32 running_sum;                    // CIC-аккумулятор автокорреляции
    float energy_b;                          // Скользящая энергия правого окна B
    int ptr;                                 // Указатель записи головы FIFO

    // === СТУПЕНЬ 2: Буферы децимированной MLS-Ищейки ===
    cplx_f32 macro_vector_history[MLS_MACRO_HISTORY_LEN]; // История
    float  angle_trace_history [MLS_MACRO_HISTORY_LEN];
    int macro_ptr;                                 // Указатель головы макро-буфера
    float decimation_accumulator;                  // Интегратор для прореживания
    int decimation_counter;                        // Счетчик отсчетов до 100

    // Растянутый шаблон знаков MLS-31 (заполняется один раз при init)
    int8_t template_mls_stretched[MLS_MACRO_HISTORY_LEN];
    int is_calibrated;
    cplx_f32 calibre;
    cplx_f32 derot;

    // === НОВАЯ ВТОРАЯ СТУПЕНЬ: Мягкий символьный интегратор ===
    float symbol_integrator;         // Копилка фазы внутри текущего символа
    float soft_mls_buffer[MLS_LEN];  // Буфер мягких решений на 31 символ (124 байта!)
    int symbol_ptr;                  // Указатель головы 31-элементного буфера

    /* детектор отрицательного пика синхропустышки -1 (ПЕРЕД MLS-31!) */
    float clk_smooth_buffer[DECLK_WINDOW];
    float clk_smooth_sum;
    int clk_smooth_ptr;
    float clk_smooth_min;
    int clk_min_hold_counter;

    float spy;
} mls_sync_t;

// Состояния автомата тактовой сетки
typedef enum {
    CLK_WAIT_PILOT_STABLE, // Ждем, пока Костас выйдет на режим
    CLK_CATCH_SYNC_SYMBOL, // Ловим холостой треугольник
    CLK_RUNNING_DECIMATOR  // Сетка защелкнута, работает ищейка MLS
} clk_grid_state_t;

void mls_init(mls_sync_t *sync);
int mls_tick(mls_sync_t *sync, const cplx_f32 *input_sample, float *out_mls_needle, float *out_sc_power);

#endif
