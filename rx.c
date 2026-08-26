#include "rx.h"
#include "config.h"
#include <stdio.h>
#include <math.h>

static complex_f prev_tone_integrals[NUM_DATA_TONES];
static int first_symbol = 1;

void rx_init(void) {
    first_symbol = 1;
    for (int i = 0; i < NUM_DATA_TONES; i++) {
        prev_tone_integrals[i].re = 0.0f;
        prev_tone_integrals[i].im = 0.0f;
    }
}

uint8_t rx_decode_symbol(complex_f *data_buffer, uint32_t absolute_symbol_idx) {
    uint8_t decoded_nibble = 0;

    // Вычисляем, с какого абсолютного сэмпла файла начинается ТЕЛО текущего символа
    // absolute_symbol_idx - номер символа от 0 до ...
    uint32_t start_sample_offset = absolute_symbol_idx * TOTAL_SYMBOL_SAMPLES + CP_SAMPLES;

    for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
        float tone_freq = data_tones[tone];
        complex_f curr_tone_integral = {0.0f, 0.0f};

        // ДПФ с абсолютной временной привязкой фазы
        for (int i = 0; i < N_SAMPLES; i++) {
            // ИСТИННОЕ ГЛОБАЛЬНОЕ ВРЕМЯ КАНАЛА:
            uint32_t global_sample_time = start_sample_offset + i;
            float t_global = (float)global_sample_time / FS;

            float cos_ref = cosf(2.0f * PI_F * tone_freq * t_global);
            float sin_ref = sinf(2.0f * PI_F * tone_freq * t_global);

            curr_tone_integral.re += data_buffer[i].re * cos_ref + data_buffer[i].im * sin_ref;
            curr_tone_integral.im += data_buffer[i].im * cos_ref - data_buffer[i].re * sin_ref;
        }

        if (first_symbol) {
            prev_tone_integrals[tone] = curr_tone_integral;
        } else {
            // Теперь разность фаз будет идеально чистой на любой частоте!
            float dot_product_im = curr_tone_integral.im * prev_tone_integrals[tone].re -
                                   curr_tone_integral.re * prev_tone_integrals[tone].im;

            if (dot_product_im > 0.0f) {
                decoded_nibble |= (1 << tone);
            }

            printf("  Тон %d (%4.0f Гц): IM_SWING = %7.2f -> БИТ = %d\n",
                   tone, tone_freq, dot_product_im, (dot_product_im > 0.0f) ? 1 : 0);

            prev_tone_integrals[tone] = curr_tone_integral;
        }
    }

    if (first_symbol) {
        printf("[Приёмник] Зафиксирован стартовый опорный символ пакета.\n");
        first_symbol = 0;
        return 0x00;
    }

    return decoded_nibble;
}

