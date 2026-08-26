#include "rx.h"
#include "config.h"
#include <math.h>

// Кольцевой буфер для алгоритма Шмидла-Кокса
// Нам нужно хранить историю длиной в один полный символ N_SAMPLES
static float rx_buffer[N_SAMPLES];
static uint32_t buf_idx = 0;

static rx_state_t rx_current_state = RX_STATE_SEARCH;
static float freq_offset_hz = 0.0f;

// Переменные для рекурсивного коррелятора
static float P_re = 0.0f;
static float P_im = 0.0f;
static float R_energy = 0.0f;

void rx_init(void) {
    rx_current_state = RX_STATE_SEARCH;
    buf_idx = 0;
    P_re = 0.0f; P_im = 0.0f; R_energy = 0.0f;
    freq_offset_hz = 0.0f;
    for (int i = 0; i < N_SAMPLES; i++) rx_buffer[i] = 0.0f;
}

float rx_get_frequency_offset(void) {
    return freq_offset_hz;
}

bool rx_process_sample(int16_t sample, uint8_t *out_bit) {
    float x_curr = (float)sample / 32768.0f; // Нормализация к ±1.0

    // Индексы для задержанных сэмплов в кольцевом буфере
    uint32_t idx_half = (buf_idx + HALF_N) % N_SAMPLES;
    uint32_t idx_full = buf_idx; // Самый старый элемент, который сейчас перезапишется

    float x_half = rx_buffer[idx_half];
    float x_full = rx_buffer[idx_full];

    // Сохраняем текущий сэмпл в буфер
    rx_buffer[buf_idx] = x_curr;
    buf_idx = (buf_idx + 1) % N_SAMPLES;

    if (rx_current_state == RX_STATE_SEARCH) {
        // Рекурсивный Шмидл-Кокс
        // В КВ-диапазоне сигнал комплексный после IQ-смесителя,
        // но так как мы на "голой физике" со звуковой карты (Real Signal),
        // мы считаем вещественную корреляцию и оцениваем энергию.

        P_re += (x_curr * x_half) - (x_half * x_full);
        R_energy += (x_half * x_half) - (x_full * x_full);

        if (R_energy > 0.001f) {
            float metric = (P_re * P_re) / (R_energy * R_energy);

            // Если метрика Шмидла-Кокса превысила жесткий порог (например, 0.85)
            if (metric > 0.85f) {
                // Поймали плато преамбулы!
                // Оценка сдвига частоты для вещественного сигнала требует аналитического фильтра (Гильберта),
                // либо для простоты на МК: смотрим сдвиг нуля между x_curr и x_half через арктангенс.
                // В демонстрационных целях заложим базовую формулу:
                float phase_diff = acosf(P_re / R_energy);
                freq_offset_hz = phase_diff / (PI_F * ((float)HALF_N / FS));

                rx_current_state = RX_STATE_DECODE; // Переходим к приему (минуя защитный интервал для упрощения)
                return false;
            }
        }
    }
    else if (rx_current_state == RX_STATE_DECODE) {
        // Здесь будет логика накопления кадра данных и BPSK демодуляция
        // (Вернем true, когда накопим N_SAMPLES и извлечем бит)
    }

    return false;
}
