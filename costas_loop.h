#ifndef COSTAS_LOOP_H
#define COSTAS_LOOP_H

#include <stdint.h>

#include <stdint.h>
#include <math.h>

typedef struct {
    // 1. Локальный генератор (NCO)
    uint16_t phase_acc;         // 16-битный накопитель фазы гетеродина
    uint16_t phase_inc;         // Шаг частоты (FTW). Инициализируется от БПФ-детектора
    uint16_t base_phase_inc;    // Базовый шаг частоты (центр удержания)

    // 2. Блок Integrate-and-Dump (Накопление на символе)
    int32_t sum_i;              // Корреляционный интегратор синфазного канала
    int32_t sum_q;              // Корреляционный интегратор квадратурного канала
    uint16_t sample_counter;    // Счетчик отсчетов внутри символа (0..255)

    // 3. Петлевой PI-фильтр 2-го порядка (Loop Filter)
    int32_t integrator;         // Интегральная ветвь фильтра (копит дрейф частоты)
    uint8_t shift_p;            // Пропорциональный коэффициент Кр (в битовых сдвигах >> N)
    uint8_t shift_i;            // Интегральный коэффициент Ki (в битовых сдвигах >> N)
} costas_loop_t;

typedef enum {
    COSTAS_MODE_ACQUISITION = 0, // Режим широкой петли (быстрый захват)
    COSTAS_MODE_TRACKING         // Режим узкой петли (стабильное слежение)
} costas_mode_t;

typedef struct {
    uint16_t phase_acc;
    uint16_t phase_inc;
    uint16_t base_phase_inc;

    // Интеграторы Integrate-and-Dump
    int32_t sum_i;
    int32_t sum_q;
    int32_t sum_energy;         // Накопление полной энергии для Lock Detector
    uint16_t sample_counter;

    // Память предыдущего символа для FLL
    int32_t prev_sum_i;
    int32_t prev_sum_q;

    // Фильтр петли
    int32_t integrator;
    costas_mode_t mode;
    int32_t lock_metric;        // Сглаженный индикатор захвата
} advanced_costas_t;

/**
 * @brief Инициализация петли Костаса по данным от БПФ-детектора
 * @param freq_offset_hz Кандидат частоты из search_pilot_in_bag (например, -44.922 Гц)
 */
void costas_init(costas_loop_t *loop, float freq_offset_hz);
void costas_process_sample(costas_loop_t *loop, int16_t in_i, int16_t in_q, int *bit_strobe, int *out_decision);

void advanced_costas_init(advanced_costas_t *loop, float freq_offset_hz);
void advanced_costas_process(advanced_costas_t *loop, int16_t in_i, int16_t in_q, int *bit_strobe, int *out_decision, int *is_locked);

#endif // COSTAS_LOOP_H
