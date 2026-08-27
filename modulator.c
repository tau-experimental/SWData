#include "modulator.h"
#include "complex_filter.h"

#define PHASE_STEP_RAD (2.0f * M_PI_F / 3.0f) // 120 градусов в радианах

float base_frequencies[NUM_TONES] = {1300.0f, 1400.0f, 1500.0f, 1600.0f};

void dsp_modulator_init(modulator_t *mod, float sample_rate, uint32_t symbol_duration) {
    mod->symbol_duration = symbol_duration;
    mod->sample_counter = 0;

    for (int i = 0; i < NUM_TONES; i++) {
        dsp_dds_init(&mod->tone_gen[i], base_frequencies[i], sample_rate);
        mod->accumulated_phases[i] = 0.0f; // Стартуем с нулевого сдвига фаз
    }
}

void dsp_modulator_step(modulator_t *mod, uint8_t nibble, complex_f *out_sample) {
    out_sample->re = 0.0f;
    out_sample->im = 0.0f;

    /* Проверяем, наступила ли граница символа (пора переключать фазы) */
    if (mod->sample_counter >= mod->symbol_duration) {
        mod->sample_counter = 0; // Сброс счетчика для нового символа

        /* Обрабатываем каждый из 4-х бит ниббла */
        for (int i = 0; i < NUM_TONES; i++) {
            // Проверяем i-й бит ниббла (0-й бит = тон 1300, ..., 3-й бит = тон 1600)
            int bit = (nibble >> i) & 0x01;

            if (bit == 1) {
                mod->accumulated_phases[i] += PHASE_STEP_RAD; // Шаг +120 градусов
            } else {
                mod->accumulated_phases[i] -= PHASE_STEP_RAD; // Шаг -120 градусов
            }

            /* Нормализуем фазу в диапазон [-PI, PI], чтобы избежать накопления погрешности float */
            if (mod->accumulated_phases[i] > M_PI_F)  mod->accumulated_phases[i] -= 2.0f * M_PI_F;
            if (mod->accumulated_phases[i] < -M_PI_F) mod->accumulated_phases[i] += 2.0f * M_PI_F;
        }
    }

    /* Генерируем сэмплы всех тонов с учетом их накопленных фазовых сдвигов */
    for (int i = 0; i < NUM_TONES; i++) {
        complex_f tone_sample;
        // Передаем текущую фазу манипуляции как смещение для DDS
        dsp_dds_next_complex(&mod->tone_gen[i], mod->accumulated_phases[i], &tone_sample);

        // Суммируем с нормированием амплитуды (0.20, оставляя запас под будущие шумы)
        out_sample->re += tone_sample.re * 0.20f;
        out_sample->im += tone_sample.im * 0.20f;
    }

    mod->sample_counter++;
}
