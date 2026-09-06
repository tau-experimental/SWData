#ifndef PREAMBLE_DETECTOR_H
#define PREAMBLE_DETECTOR_H

#include <stdint.h>

#define MLS_MAX_LEN 64 // Максимальный запас под MLS-63

typedef struct {
    int16_t sample_buffer[MLS_MAX_LEN]; // Скользящее окно принятых мягких символов (I)
    int8_t  mls_pattern[MLS_MAX_LEN];   // Опорный паттерн MLS, переведенный в жесткие знаки (+1 / -1)
    uint8_t mls_len;                    // Текущая рабочая длина (31 или 63)
} preamble_detector_t;

/**
 * @brief Инициализация детектора преамбулы
 * @param det Указатель на структуру
 * @param len Длина последовательности (31 или 63)
 */
void preamble_init(preamble_detector_t *det, uint8_t len);

/**
 * @brief Продвижение скользящего окна коррелятора
 * @details Вызывается строго на символьной скорости (когда clk.symbol_ready == 1).
 *          Задвигает новое мягкое решение в буфер и считает текущую свертку.
 * @param det Указатель на структуру
 * @param rx_symbol_i Мягкое решение (амплитуда I) из центра символа
 * @return Текущее абсолютное значение корреляции (в fixed-point масштабе)
 */
int32_t preamble_process(preamble_detector_t *det, int16_t rx_symbol_i);

#endif // PREAMBLE_DETECTOR_H
