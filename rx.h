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

#define ALPHA_POLE 0.98058f   // Полоса BPF ~50 Гц при Fs=8000
#define HOLD_TIME_SAMPLES 24  // Окно удержания Hold-триггера
#define LOOP_GAIN 0.02f       // Коэффициент фильтра петли АПЧ (скорость сходимости)

// ============================================================================
// СТРУКТУРЫ ХРАНЕНИЯ СОСТОЯНИЙ КАСКАДОВ (КОНТЕКСТЫ)
// ============================================================================

// Каскад 1: Входной комплексный смеситель АПЧ
typedef struct {
    float phase;             // Текущая фаза гетеродины [-PI, PI]
    float freq_correction;   // Накопленная коррекция частоты (freq.delta) в рад/сэмпл
} mixer_stage_t;

typedef struct {
    float freq_target;
    float R_coeff;
    float K_coeff;
    float gain_scale;

    // Каскад А (Звено 1)
    float i_a1; float i_a2;
    float q_a1; float q_a2;

    // Каскад Б (Звено 2) - Включен последовательно после А
    float i_b1; float i_b2;
    float q_b1; float q_b2;

    complex_f y_curr;
    complex_f y_prev;
} bpf_stage_t;


extern bpf_stage_t rx_bpf_bank[];
extern float ch_cfo_estimates[];
extern float debug_smooth_cfo_hz, smooth_cfo_rad;
extern float last_total_theta[];
extern float phase_stability_acc[];

// Каскад 4: Независимый фазовый детектор
typedef struct {
    float last_phase;        // Предыдущее значение фазы
    uint32_t hold_counter;   // Таймер оконного удержания
    bool is_switching;       // Флаг детекции скачка фазы в канале
} pd_stage_t;

float rx_debug_get_ch_cfo(int tone_idx);
float rx_debug_get_mixer_correction_hz(void);
bool  rx_debug_get_ch_switching(int tone_idx);
void rx_pipeline_init(void);

#endif // RX_H
