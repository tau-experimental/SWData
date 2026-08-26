#include "tx.h"
#include "config.h"
#include <math.h>

const float data_tones[NUM_DATA_TONES] = {1000.0f, 1200.0f, 1400.0f, 1600.0f};

static float dds_sine_table[SINE_TABLE_SIZE];
static uint32_t dds_phase_acc[NUM_DATA_TONES];
static uint8_t dds_phase_shifts[NUM_DATA_TONES];
static uint32_t tx_sample_counter = 0;

void tx_init(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        dds_sine_table[i] = sinf((2.0f * PI_F * i) / SINE_TABLE_SIZE);
    }
    for (int i = 0; i < NUM_DATA_TONES; i++) {
        dds_phase_acc[i] = 0;
        dds_phase_shifts[i] = 0;
    }
    uint32_t tx_sample_counter = 0;
}

// Шаги приращения фазы для 32-битного DDS при FS=8000 Гц
static const uint32_t dds_increments[NUM_DATA_TONES] = {
    536870912UL, // 1000 Гц
    644245094UL, // 1200 Гц
    751619276UL, // 1400 Гц
    858993459UL  // 1600 Гц
};

void tx_step_phase(uint8_t nibble) {
    for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
        uint8_t bit = (nibble >> tone) & 0x01;
        if (bit == 0) {
            // Шаг назад на 90 градусов (-64 отсчета в таблице)
            dds_phase_shifts[tone] = (dds_phase_shifts[tone] - PHASE_90_SHIFT + SINE_TABLE_SIZE) % SINE_TABLE_SIZE;
        } else {
            // Шаг вперед на 90 градусов (+64 отсчета в таблице)
            dds_phase_shifts[tone] = (dds_phase_shifts[tone] + PHASE_90_SHIFT) % SINE_TABLE_SIZE;
        }
    }
    uint32_t tx_sample_counter = 0;
}

void tx_get_sample(complex_f *out) {
    out->re = 0.0f;
    out->im = 0.0f;

    for (int tone = 0; tone < NUM_DATA_TONES; tone++) {
        dds_phase_acc[tone] += dds_increments[tone];
        uint8_t base_idx = (uint8_t)(dds_phase_acc[tone] >> 24);

        uint8_t idx_re = (base_idx + dds_phase_shifts[tone] + 64) % SINE_TABLE_SIZE; // Косинус
        uint8_t idx_im = (base_idx + dds_phase_shifts[tone]) % SINE_TABLE_SIZE;      // Синус

        out->re += 0.25f * dds_sine_table[idx_re];
        out->im += 0.25f * dds_sine_table[idx_im];
    }
}
