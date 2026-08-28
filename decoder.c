#include "decoder.h"
#include <math.h>

void dsp_decoder_init(dsp_decoder_t *dec) {
    dec->current_nibble = 0;
}

/* Вспомогательная нормализация угла в диапазон [-PI, PI] */
static inline float normalize_angle(float angle) {
    while (angle > M_PI_F)  angle -= 2.0f * M_PI_F;
    while (angle < -M_PI_F) angle += 2.0f * M_PI_F;
    return angle;
}

int dsp_decoder_process_strobe(dsp_decoder_t *dec, const complex_f *diff_outputs) {
    float phases[NUM_TONES];
    int bits[NUM_TONES];
    static int frame_synced = 0; // Флаг: нашли ли мы маркер начала кадра

    for (int i = 0; i < NUM_TONES; i++) {
        phases[i] = atan2f(diff_outputs[i].im, diff_outputs[i].re);
    }

    if (phases[0] >= 0.0f) bits[0] = 1; else bits[0] = 0;

    for (int i = 1; i < NUM_TONES; i++) {
        float delta_phase = normalize_angle(phases[i] - phases[0]);
        if (fabs(delta_phase * 180.0f / M_PI_F) < 60.0f) bits[i] = bits[0];
        else bits[i] = 1 - bits[0];
    }

    uint8_t nibble = 0;
    for (int i = 0; i < NUM_TONES; i++) nibble |= (bits[i] << i);
    dec->current_nibble = nibble;
    printf("0x%X\n", nibble);

#if 0
    /* === БЛОК СИНХРОНИЗАЦИИ СЕТКИ НИББЛОВ === */
    if (!frame_synced) {
        // Мы ищем стартовый маркер. Пусть это будет ниббл 0xA.
        // Пока его нет — сбрасываем триггер сборки и не выдаем байты в консоль
        if (nibble == 0x0A) {
            frame_synced = 1;
            dec->nibble_toggle = 0; // СЛЕДУЮЩИЙ ниббл гарантированно будет ВЕРХНИМ!
            printf("[DECODER] Маркер кадра 0xA обнаружен! Сетка нибблов выровнена.\n");
        }
        return 0;
    }
    /* ======================================= */

    // Обычная конвейерная сборка байта

    if (dec->nibble_toggle == 0) {
        dec->received_byte = (nibble << 4);
        dec->nibble_toggle = 1;
        return 0;
    } else {
        dec->received_byte |= (nibble & 0x0F);
        dec->nibble_toggle = 0;
        return 1;
    }
#endif
    return 1;
}

