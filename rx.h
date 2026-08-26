#ifndef RX_H
#define RX_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// Состояния приемника
typedef enum {
    RX_STATE_SEARCH,      // Поиск преамбулы (автокорреляция)
	RX_STATE_PREAMBLE_CALIBRATE,
    RX_STATE_DECODE       // Демодуляция символов данных
} rx_state_t;

#define SLIDING_WIN_LEN 128 // Выросло до 128 для когерентной устойчивости
#define SEARCH_WIN_LEN 160  // Возвращаем ультра-короткое окно для нечувствительности к КВ-дрейфу!


#define ALPHA_POLE 0.98058f  // Коэффициент затухания для полосы ~50 Гц при Fs=8000
#define HOLD_TIME_SAMPLES 24 // Время удержания флага подозрения (окно триггера)

// Структура одного узкополосного канала
typedef struct {
    float freq_target;       // Целевая частота тона (1000, 1200, 1400, 1600)
    float cos_w0;            // Предвычисленный косинус шага фазы
    float sin_w0;            // Предвычисленный синус шага фазы

    complex_f y_prev;        // Состояние комплексного резонатора Гёрцеля (Y[n-1])
    complex_f y_delayed;     // Задержанный выход для автокоррелятора (опционально для больших окон)

    // Метрики канала
    float cfo_error_hz;      // Текущая частотная расстройка в этом канале (Гц)
    float phase_jump_metric; // Метрика скачка фазы (нестабильность угла)

    uint32_t hold_counter;   // Счётчик Hold-таймера удержания детекции
    bool is_switching;       // Флаг: в канале зафиксировано переключение фазы
} channel_filter_t;

extern channel_filter_t rx_channels[];
extern float dds_sine_table[];

void rx_init(void);

uint8_t rx_decode_symbol(complex_f *data_buffer, uint32_t absolute_symbol_idx);

extern float dpll_error_accumulator;

#endif // RX_H
