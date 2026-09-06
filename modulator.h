#ifndef MODULATOR_H
#define MODULATOR_H

#include <stdint.h>

// Режимы работы физического уровня передатчика (Состояния FSM)
typedef enum {
    MOD_STATE_IDLE = 0,         // Передатчик молчит
    MOD_STATE_PILOT,            // Передача непрерывного пилот-тона
    MOD_STATE_PREAMBLE,         // Передача MLS-преамбулы (BPSK)
    MOD_STATE_DATA_DBPSK,       // Передача данных в режиме DBPSK
    MOD_STATE_DATA_DQPSK        // Передача данных в режиме pi/4-DQPSK
} mod_state_t;

typedef struct {
    // Настройки частоты и таймингов
    uint16_t phase_acc;         // 16-битный аккумулятор фазы несущей (DDS)
    uint16_t phase_inc;         // Шаг частоты (FTW). Для 1000 Гц при 8 кГц равен 8192

    // Автомат передачи
    mod_state_t state;          // Текущее состояние (Пилот, Преамбула, Данные...)
    uint32_t state_counter;     // Общий счетчик отсчетов для текущего состояния (например, длительность пилота)

    // Переменные текущего символа
    uint16_t sample_in_sym;     // Счетчик отсчетов внутри ОДНОГО символа (0..255)
    uint16_t symbol_idx;        // Индекс текущего передаваемого символа из буфера
    uint16_t tx_phase_accum;    // Накопленная абсолютная фаза связи (манипуляции)

    // Источник данных
    const uint8_t *tx_buffer;   // Указатель на заранее подготовленный буфер символов/бит
    uint16_t tx_buffer_len;     // Размер буфера данных в элементах
} modulator_t;

extern const int16_t sin_lut_256[256];

// Инициализация (настройка несущей)
void modulator_init(modulator_t *mod, uint32_t carrier_freq, uint32_t sample_rate);

// Запуск передачи определенного этапа
void modulator_start_pilot(modulator_t *mod, uint32_t duration_samples);
void modulator_start_data(modulator_t *mod, const uint8_t *buffer, uint16_t len, mod_state_t mode);

// Главная функция: вызывается строго 8000 раз в секунду (из прерывания или цикла симулятора)
// Записывает ровно одну пару I/Q значений в переданные указатели
void modulator_get_next_sample(modulator_t *mod, int16_t *out_i, int16_t *out_q);

#endif // MODULATOR_H
