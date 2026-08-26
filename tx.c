#include "tx.h"
#include "config.h"
#include <math.h>

static uint32_t sample_counter = 0;

void tx_init(void) {
    sample_counter = 0;
}

int16_t tx_get_next_sample(tx_state_t state, uint8_t data_bit) {
    float out_val = 0.0f;

    if (state == TX_STATE_PREAMBLE) {
        // Генерируем сумму двух тонов 1200 и 1800 Гц
        // Для N=160 они автоматически дадут две одинаковые половины
        float t = (float)sample_counter / FS;
        out_val = 0.5f * sinf(2.0f * PI_F * FREQ_TONE1 * t) +
                  0.5f * sinf(2.0f * PI_F * FREQ_TONE2 * t);

        sample_counter++;
    }
    else if (state == TX_STATE_DATA) {
        // Упрощенный пример для одного тона (BPSK)
        float t = (float)sample_counter / FS;
        float phase_shift = (data_bit == 1) ? PI_F : 0.0f;

        out_val = sinf(2.0f * PI_F * FREQ_TONE1 * t + phase_shift);

        sample_counter++;
    }

    // Сброс счетчика по границе символа во избежание переполнения во времени
    if (sample_counter >= N_SAMPLES) {
        sample_counter = 0;
    }

    // Переводим float в формат int16_t для ЦАП / WAV
    return (int16_t)(out_val * 16384.0f); // Оставляем запас по громкости 50%
}
