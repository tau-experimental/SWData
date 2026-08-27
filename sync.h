#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SYNC_STATE_SEARCH,     // Поиск первого пика
    SYNC_STATE_CHECK_1,    // Найден один интервал (нужно подтверждение)
    SYNC_STATE_LOCKED      // Захват выполнен (работает DPLL / выдача строба)
} sync_state_t;

typedef struct {
    sync_state_t state;
    uint32_t expected_symbol_len; // Ожидаемая длина символа (например, 800)
    uint32_t allowed_jitter;       // Допустимый дрейф/погрешность (например, 20 сэмплов)

    uint32_t last_peak_time;       // Время (в сэмплах) последнего обнаруженного пика
    uint32_t first_interval;       // Величина первого замеренного интервала

    // Переменные для дифференциального детектора пика
    float s_prev;
    float s_pprev;
    float noise_floor; // Оценка уровня шума/покоя
    uint32_t blanking_timer; // Счётчик запрета поиска пиков
    uint32_t peak_count; // просто счётчик пиков, сколько их там вообще найдено - надо сбрасывать в начале каждого нового сигнала!
    bool ascending;

    uint32_t sample_counter;       // Абсолютный счетчик сэмплов с момента старта приемника
    uint32_t symbol_timer;         // Таймер для удержания синхронизации в режиме Locked
} clock_recovery_t;

void dsp_sync_init(clock_recovery_t *sync, uint32_t symbol_len);

/* Посэмплный обработчик синхронизации.
   Возвращает true строго (гипотетически) в тот сэмпл, когда нужно декодировать ниббл (Строб). */
bool dsp_sync_step(clock_recovery_t *sync, float s_curr);

#endif /* SYNC_H */
