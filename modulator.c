#include "modulator.h"

#define SAMPLES_PER_SYMBOL 256  // 31.25 Бод при 8000 Гц
#define RAMP_SAMPLES       16   // 2 мс на плавное сглаживание углов фазы

// Статическая таблица синуса на 256 точек в FLASH
const int16_t sin_lut_256[256] = {
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
      6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
     12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
     23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
     27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
     32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
     32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
     30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
     27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
     18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
     12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
         0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804
};


static inline int16_t lut_sin(uint16_t phase) {
    return sin_lut_256[phase >> 8];
}

static inline int16_t lut_cos(uint16_t phase) {
    // Добавление 64 к uint8_t автоматически сделает wrap-around вокруг 255 без всяких условий!
    uint8_t idx = (phase >> 8) + 64;
    return sin_lut_256[idx];
}

void modulator_init(modulator_t *mod, uint32_t carrier_freq, uint32_t sample_rate) {
    mod->phase_inc = (uint32_t)(((uint64_t)carrier_freq * 65536) / sample_rate);
    mod->phase_acc = 0;
    mod->state = MOD_STATE_IDLE;
}

void modulator_start_pilot(modulator_t *mod, uint32_t duration_samples) {
    mod->state = MOD_STATE_PILOT;
    mod->state_counter = duration_samples;
    mod->tx_phase_accum = 0;
}

void modulator_start_data(modulator_t *mod, const uint8_t *buffer, uint16_t len, mod_state_t mode) {
    mod->state = mode;
    mod->tx_buffer = buffer;
    mod->tx_buffer_len = len;
    mod->symbol_idx = 0;
    mod->sample_in_sym = 0;
    mod->state_counter = 0;
    mod->tx_phase_accum = 0;
}

// Внутренний шаг вычисления фазы для нового символа
static void advance_to_next_symbol(modulator_t *mod) {
    mod->sample_in_sym = 0;
    mod->symbol_idx++;

    // Если буфер символов кончился — тушим передатчик
    if (mod->symbol_idx >= mod->tx_buffer_len) {
        mod->state = MOD_STATE_IDLE;
        return;
    }

    uint8_t sym_bits = mod->tx_buffer[mod->symbol_idx];

    if (mod->state == MOD_STATE_PREAMBLE) {
        // Абсолютный BPSK для MLS (0 или 180 градусов)
        mod->tx_phase_accum = (sym_bits & 1) ? 32768 : 0;
    }
    else if (mod->state == MOD_STATE_DATA_DBPSK) {
        // Дифференциальный BPSK (0 -> фаза на месте, 1 -> инверсия)
        uint16_t delta = (sym_bits & 1) ? 32768 : 0;
        mod->tx_phase_accum = (mod->tx_phase_accum + delta) & 0xFFFF;
    }
    else if (mod->state == MOD_STATE_DATA_DQPSK) {
        // Опциональный pi/4-DQPSK (Маппинг Грея на сдвиги фаз)
        uint16_t delta = 0;
        switch (sym_bits & 0x03) {
            case 0x00: delta = 8192;   break; // +45°
            case 0x01: delta = 24576;  break; // +135°
            case 0x03: delta = 40960;  break; // -135°
            case 0x02: delta = 57344;  break; // -45°
        }
        mod->tx_phase_accum = (mod->tx_phase_accum + delta) & 0xFFFF;
    }
}

void modulator_get_next_sample(modulator_t *mod, int16_t *out_i, int16_t *out_q) {
    if (mod->state == MOD_STATE_IDLE) {
        *out_i = 0; *out_q = 0;
        return;
    }

    // [Пилот-тон остается без изменений]
    if (mod->state == MOD_STATE_PILOT) {
        *out_i = lut_cos(mod->phase_acc);
        *out_q = lut_sin(mod->phase_acc);
        mod->phase_acc = (mod->phase_acc + mod->phase_inc) & 0xFFFF;
        if (mod->state_counter > 0) {
            mod->state_counter--;
            if (mod->state_counter == 0) mod->state = MOD_STATE_IDLE;
        }
        return;
    }

    // --- СИМВОЛЬНЫЕ РЕЖИМЫ (Преамбула / Данные) ---
    // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Вычисляем параметры символа ДО тригонометрии
    if (mod->sample_in_sym == 0) {
        // Проверяем, не вылетели ли мы за пределы буфера данных
        if (mod->symbol_idx >= mod->tx_buffer_len) {
            mod->state = MOD_STATE_IDLE;
            *out_i = 0; *out_q = 0;
            return;
        }

        uint8_t sym_bits = mod->tx_buffer[mod->symbol_idx];

        if (mod->state == MOD_STATE_PREAMBLE) {
            mod->tx_phase_accum = (sym_bits & 1) ? 32768 : 0; // Настоящий BPSK 0/180
        }
        else if (mod->state == MOD_STATE_DATA_DBPSK) {
            uint16_t delta = (sym_bits & 1) ? 32768 : 0;
            mod->tx_phase_accum = (mod->tx_phase_accum + delta) & 0xFFFF;
        }
        else if (mod->state == MOD_STATE_DATA_DQPSK) {
            uint16_t delta = 0;
            switch (sym_bits & 0x03) {
                case 0x00: delta = 8192;   break; // +45°
                case 0x01: delta = 24576;  break; // +135°
                case 0x03: delta = 40960;  break; // -135°
                case 0x02: delta = 57344;  break; // -45°
            }
            mod->tx_phase_accum = (mod->tx_phase_accum + delta) & 0xFFFF;
        }
    }

    // Берем отсчеты синуса и косинуса на текущей полной фазе
    uint16_t total_phase = (mod->phase_acc + mod->tx_phase_accum) & 0xFFFF;
    int16_t raw_i = lut_cos(total_phase);
    int16_t raw_q = lut_sin(total_phase);

    // Сглаживание амплитуды (Pulse Shaping)
    int32_t scale = 32768;
    if (mod->state != MOD_STATE_PREAMBLE) { // На MLS прямоугольник оставляем нетронутым
        if (mod->sample_in_sym < RAMP_SAMPLES) {
            scale = (mod->sample_in_sym * 32768) / RAMP_SAMPLES;
        } else if (mod->sample_in_sym > (SAMPLES_PER_SYMBOL - RAMP_SAMPLES)) {
            int samples_from_end = SAMPLES_PER_SYMBOL - mod->sample_in_sym;
            scale = (samples_from_end * 32768) / RAMP_SAMPLES;
        }
    }

    // Вывод в каналы I и Q
    *out_i = (int16_t)((raw_i * scale) >> 15);
    *out_q = (int16_t)((raw_q * scale) >> 15);

    // Логика шага DDS
    mod->phase_acc = (mod->phase_acc + mod->phase_inc) & 0xFFFF;

    // Переход к следующему отсчету/символу
    mod->sample_in_sym++;
    if (mod->sample_in_sym >= SAMPLES_PER_SYMBOL) {
        mod->sample_in_sym = 0;
        mod->symbol_idx++; // Инкремент индекса произойдет здесь, а отработает на следующем тике
    }
}
