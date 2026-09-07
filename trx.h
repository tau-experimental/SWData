#ifndef __TRX__MACHINE__
#define __TRX__MACHINE__

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#define FS 8000
#define BAUDRATE 31.25
#define SAMPLES_PER_SYMBOL (int)(FS / BAUDRATE) // 256 сэмплов

#define TONE_BASE	800.0f
#define TONE_STEP	31.25f
#define TONE(n)	((float)(TONE_BASE + (n)*TONE_STEP))
typedef enum { TONE_A, TONE_B, TONE_C, TONE_D } Tone_t;

extern int16_t SinTab[256];
#define MLS31_LENGTH	31
extern const int8_t mls_31_polar[MLS31_LENGTH];
void generate_test_packet(float *buf_i, float *buf_q, int *total_samples);

// Состояния автомата
typedef enum {
    STATE_INITIAL_SILENCE,
    STATE_LEAD_IN_PILOT,
    STATE_PREAMBLE,
    STATE_DATA,
    STATE_TRAILER_PILOT,
    STATE_FINAL_SILENCE,
    STATE_IDLE
} tx_state_t;

#include "dds.h"

// Контекст автомата передатчика
typedef struct {
    tx_state_t state;
    dds_t dds;
    uint32_t sample_counter;   // Счетчик сэмплов внутри текущего состояния
    uint32_t symbol_counter;   // Счетчик переданных символов (для преамбулы)
    bool is_active;            // Флаг работы передатчика

    // Специфичные поля для блока данных
    const uint8_t *raw_data_bytes; // Указатель на массив символов (0..3)
    uint16_t total_bytes; // Общее количество символов в пакете
    uint16_t dibit_index;        // Текущий индекс дибита для scramble_iterate
} tx_machine_t;

// Контекст демодулятора
typedef struct {
	cplx_f32 integrators[4]; // Накопители для 4-х тонов
    uint32_t sample_idx;       // Счетчик сэмплов внутри символа
} mfsk_demod_t;

typedef enum {
    SYNC_SEARCH_PILOT,  // Ищем стабильный пилот-тон
    SYNC_WAIT_PREAMBLE, // Пилот кончился, ищем пик корреляции MLS
    SYNC_LOCKED         // Синхронизация успешно поймана!
} sync_state_t;

typedef struct {
    sync_state_t state;
    cplx_f32 integrators[2]; // Считаем ДПФ только для TONE_A и TONE_B
    uint32_t sample_idx;

    // Буфер для поиска пика MLS
    float mls_energy_history[MLS31_LENGTH];
    uint8_t history_head;

    uint32_t pilot_detect_counter;
    uint32_t sync_sample_position; // Итоговая точная координата начала данных
} rx_sync_t;

// Структура локального гетеродина приемника (аналогично передатчику)
typedef struct {
    uint32_t phase_accumulator;
    uint32_t phase_step;
} rx_lo_t;

void rx_lo_init(rx_lo_t *lo);
cplx_f32 rx_lo_get_complex(rx_lo_t *lo);

// Инициализация передачи нового кадра
void tx_machine_start(tx_machine_t *tx, const uint8_t *data, uint16_t data_len);
// Генерация одного I/Q сэмпла «на лету»
// Возвращает true, если кадр еще передается, и false, если трансляция окончена
bool tx_machine_process_sample(tx_machine_t *tx, float *out_i, float *out_q);

// Сброс накопителей перед новым символом
void mfsk_demod_reset_symbol(mfsk_demod_t *demod);

// Обработка одного сэмпла "на лету"
// Возвращает true, если символ завершен и решение готово
bool mfsk_demod_process_sample(mfsk_demod_t *demod, cplx_f32 in_sample, uint8_t *out_symbol);

void rx_sync_init(rx_sync_t *sync);
bool rx_sync_process_sample(rx_sync_t *sync, cplx_f32 in_sample, uint32_t global_sample_idx);
bool rx_search_pilot_efficient(rx_lo_t *lo, cplx_f32 in_sample, float *out_dc_power);
#endif
