#ifndef BARKER_SYNC_H
#define BARKER_SYNC_H

#include "wav_io.h"

typedef struct {
    cplx_f32 symbol_history[6]; // Кольцевой буфер для 6 последних принятых символов
    cplx_f32 template_barker[6]; // Шаблон идеальных комплексных секторов Баркера
    cplx_f32 current_integrator; // Интегратор для накопления 800 отсчетов текущего символа
    int sample_counter;          // Счетчик отсчетов внутри одного символа (0..799)
} barker_sync_t;

typedef struct {
    cplx_f32 symbol_history[6]; // Кольцевой буфер для 6 последних принятых символов
    cplx_f32 template_barker[6]; // Шаблон идеальных комплексных секторов Баркера
    // Элементы скользящего CIC-интегратора на 800 отсчетов
    cplx_f32 delay_line[800];     // Линия задержки для «вычитания» старого отсчета
    cplx_f32 running_sum;         // Текущая бегущая сумма интегратора
    int delay_idx;                // Индекс кольцевого буфера линии задержки
} barker_sliding_t;

/*
 * Упакуем 11 бит Баркера + 1 технический ноль в 6 DQPSK дибитов (по маске Грея):
 * Биты: 11 → Символ 0 → Угол: -135° (-3π/4)
 * Биты: 10 → Символ 1 → Угол: -45° (-π/4)
 * Биты: 00 → Символ 2 → Угол: +45° (+π/4)
 * Биты: 01 → Символ 3 → Угол: +135° (+3π/4)
 * Биты: 00 → Символ 4 → Угол: +45° (+π/4)
 * Биты: 10 → Символ 5 → Угол: -45° (-π/4)
 */
// Инициализация синхронизатора (генерация эталонных векторов)
void barker_sync_init(barker_sync_t *sync);

// Обработка одного отсчета ПОСЛЕ ФАПЧ (вызывается 8000 раз в сек)
// Возвращает 1, если обнаружен пик Баркера (засечка времени), иначе 0
int barker_sync_tick(barker_sync_t *sync, const cplx_f32 *pll_output_sample, float *out_corr_power);

int barker_sliding_tick(barker_sliding_t *sync, const cplx_f32 *pll_output_sample, float *out_corr_power);
void barker_sliding_init(barker_sliding_t *sync);

#endif // BARKER_SYNC_H
