#ifndef BARKER_SYNC_H
#define BARKER_SYNC_H

#include "wav_io.h"

typedef struct {
    cplx_f32 symbol_history[6]; // Кольцевой буфер для 6 последних принятых символов
    cplx_f32 template_barker[6]; // Шаблон идеальных комплексных секторов Баркера
    cplx_f32 current_integrator; // Интегратор для накопления 800 отсчетов текущего символа
    int sample_counter;          // Счетчик отсчетов внутри одного символа (0..799)
} barker_sync_t;

// Инициализация синхронизатора (генерация эталонных векторов)
void barker_sync_init(barker_sync_t *sync);

// Обработка одного отсчета ПОСЛЕ ФАПЧ (вызывается 8000 раз в сек)
// Возвращает 1, если обнаружен пик Баркера (засечка времени), иначе 0
int barker_sync_tick(barker_sync_t *sync, const cplx_f32 *pll_output_sample, float *out_corr_power);

#endif // BARKER_SYNC_H
