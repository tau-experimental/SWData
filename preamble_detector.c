#include "preamble_detector.h"
#include <string.h>

// Каноническая MLS-31 (генератор на регистре сдвига 5 бит, полином)
static const int8_t pattern_mls31[31] = {
    1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, -1, -1,
   -1, -1, 1, -1, 1, -1, 1, 1, 1, -1, 1, 1, -1, -1, -1
};

// Заготовка под MLS-63 (полином)
static const int8_t pattern_mls63[63] = {
    1, 1, 1, 1, 1, 1, -1, 1, 1, 1, 1, -1, -1, 1, 1, -1,
    1, -1, -1, -1, 1, 1, -1, -1, 1, -1, 1, -1, 1, -1, -1, 1,
   -1, -1, -1, -1, -1, 1, -1, -1, 1, 1, 1, -1, -1, -1, 1, -1,
    1, 1, -1, 1, -1, 1, 1, -1, -1, -1, -1, 1, -1, 1, -1
};

void preamble_init(preamble_detector_t *det, uint8_t len) {
    det->mls_len = len;
    memset(det->sample_buffer, 0, sizeof(det->sample_buffer));

    if (len == 31) {
        for (int i = 0; i < 31; i++) det->mls_pattern[i] = pattern_mls31[i];
    } else {
        // Если выберем 63, подтянется этот паттерн
        for (int i = 0; i < 63; i++) det->mls_pattern[i] = pattern_mls63[i];
    }
}

int32_t preamble_process(preamble_detector_t *det, int16_t rx_symbol_i) {
    // 1. Сдвигаем буфер влево, освобождая место под новый символ
    // Для CH32V307 этот цикл на 31/63 итерации абсолютно бесплатен
    for (int i = 0; i < det->mls_len - 1; i++) {
        det->sample_buffer[i] = det->sample_buffer[i + 1];
    }
    // Пишем новое мягкое решение в конец буфера
    det->sample_buffer[det->mls_len - 1] = rx_symbol_i;

    // 2. Считаем скалярное произведение (свертку)
    int32_t correlation = 0;
    for (int i = 0; i < det->mls_len; i++) {
        // Перемножаем мягкое решение на жесткий знак эталона (+1 / -1)
        correlation += (int32_t)det->sample_buffer[i] * det->mls_pattern[i];
    }

    // Возвращаем модуль корреляции (нам важен сам пик, вне зависимости от инверсии фазы канала)
    if (correlation < 0) return -correlation;
    return correlation;
}
