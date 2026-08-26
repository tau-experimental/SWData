#include "tx.h"
#include "config.h"
#include <math.h>

static uint32_t tx_sample_counter = 0;
static uint8_t current_data_nibble = 0;

// Внутри tx.c храним фазовое состояние 4 тонов (0 - нормальная, 1 - инвертированная)
static uint8_t tx_tone_phases[NUM_DATA_TONES] = {0, 0, 0, 0};
static bool tx_phases_initialized = false;

// Перед началом отправки данных (например, в tx_init) сбрасываем их в 0
void tx_init_payload(void) {
    for(int i=0; i<NUM_DATA_TONES; i++) tx_tone_phases[i] = 0;
}

void tx_init(void) {
    tx_sample_counter = 0;
}

void tx_set_data_nibble(uint8_t nibble) {
    current_data_nibble = nibble;
}

// Передатчик теперь возвращает комплексную структуру для одного момента времени
void tx_get_next_iq_sample(tx_state_t state, complex_f *out) {
    out->re = 0.0f;
    out->im = 0.0f;

    switch(state) {
    	case TX_STATE_PREAMBLE: {
			float t = (float)tx_sample_counter / FS;
			// Преамбула Шмидла-Кокса: аналитический (IQ) сигнал двух тонов
			out->re = 0.5f * cosf(2.0f * PI_F * FREQ_TONE1 * t) + 0.5f * cosf(2.0f * PI_F * FREQ_TONE2 * t);
			out->im = 0.5f * sinf(2.0f * PI_F * FREQ_TONE1 * t) + 0.5f * sinf(2.0f * PI_F * FREQ_TONE2 * t);
			tx_sample_counter++;
			if (tx_sample_counter >= N_SAMPLES) tx_sample_counter = 0;
			tx_phases_initialized = false; // Сбрасываем фазы DPSK перед блоком данных
		}; break;

    	case TX_STATE_DATA: {
            uint32_t logic_sample_idx = tx_sample_counter;

            // Инициализация фаз при первом входе в режим данных
            if (!tx_phases_initialized) {
                for (int i = 0; i < NUM_DATA_TONES; i++) {
                    tx_tone_phases[i] = 0;
                }
                tx_phases_initialized = true;
            }

            // Обработка циклического префикса (CP)
            if (tx_sample_counter < CP_SAMPLES) {
                logic_sample_idx = tx_sample_counter + (N_SAMPLES - CP_SAMPLES);
            } else {
                logic_sample_idx = tx_sample_counter - CP_SAMPLES;
            }

            // Момент смены символа (в нашем тесте символ один, но закладываем логику для потока)
            // Дифференциальное кодирование происходит на первом сэмпле нового символа (после CP)
            if (tx_sample_counter == CP_SAMPLES) {
                for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
                    uint8_t bit = (current_data_nibble >> tone) & 0x01;
                    // DPSK: если бит == 1, инвертируем фазу относительно предыдущего символа
                    if (bit == 1) {
                        tx_tone_phases[tone] ^= 1;
                    }
                }
            }

            float t = (float)logic_sample_idx / FS;

            for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
                float phase_modifier = (tx_tone_phases[tone] == 1) ? -1.0f : 1.0f;

                out->re += 0.25f * cosf(2.0f * PI_F * data_tones[tone] * t) * phase_modifier;
                out->im += 0.25f * sinf(2.0f * PI_F * data_tones[tone] * t) * phase_modifier;
            }

            tx_sample_counter++;
            if (tx_sample_counter >= TOTAL_SYMBOL_SAMPLES) {
                tx_sample_counter = 0; // Сброс по границе полного кадра (192 сэмпла)
            }
    	}; break;

    	default: return;
    };

    if (tx_sample_counter >= TOTAL_SYMBOL_SAMPLES) {
        tx_sample_counter = 0;
    }
}

