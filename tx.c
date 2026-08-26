#include "tx.h"
#include "config.h"
#include <math.h>

static uint32_t tx_sample_counter = 0;
static uint8_t current_data_nibble = 0;

// Внутри tx.c храним фазовое состояние 4 тонов (0 - нормальная, 1 - инвертированная)
const float data_tones[NUM_DATA_TONES] = {1000.0f, 1200.0f, 1400.0f, 1600.0f};
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
    // DPSK кодирование происходит ЗДЕСЬ, один раз за символ!
    for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
        uint8_t bit = (nibble >> tone) & 0x01;
        if (bit == 1) {
            tx_tone_phases[tone] ^= 1; // Инвертируем фазовое состояние тона
        }
    }
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
    	    // Время непрерывно растет от 0 до 191 на протяжении ВСЕГО кадра данных (CP + ТЕЛО)
    	    // Чтобы в начале (от 0 до 31) получился хвост символа, мы вводим фазовый сдвиг назад
    	    // на величину (N_SAMPLES - CP_SAMPLES)

    	    float t_base = (float)tx_sample_counter / FS;

    	    // Суммируем плотный, ортогональный OFDM-аккорд
    	    for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
    	        float tone_freq = data_tones[tone];
    	        float phase_modifier = (tx_tone_phases[tone] == 1) ? -1.0f : 1.0f;

    	        // Математически строгий непрерывный циклический префикс:
    	        // Сдвигаем фазу каждого тона так, чтобы префикс идеально совпал с хвостом
    	        float cp_phase_shift = 2.0f * PI_F * tone_freq * ((float)(N_SAMPLES - CP_SAMPLES) / FS);

    	        float angle = 2.0f * PI_F * tone_freq * t_base - cp_phase_shift;

    	        out->re += 0.25f * cosf(angle) * phase_modifier;
    	        out->im += 0.25f * sinf(angle) * phase_modifier;
    	    }

    	    tx_sample_counter++;
    	    if (tx_sample_counter >= TOTAL_SYMBOL_SAMPLES) {
    	        tx_sample_counter = 0; // Сброс строго по границе полного кадра (192 сэмпла)
    	    }
        }; break;

    	default: return;
    };

    if (tx_sample_counter >= TOTAL_SYMBOL_SAMPLES) {
        tx_sample_counter = 0;
    }
}

