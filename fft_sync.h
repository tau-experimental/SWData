#ifndef FFT_SYNC_H
#define FFT_SYNC_H

#include "wav_io.h" // For cplx_f32 type
#include <stdint.h>

#define FFT_SIZE 256
#define BIN_RESOLUTION (8000/256.0f) // 8000 Гц / 256
#define FIR_TAPS 32
#define FIR_TAPX 64

#define PILOT_MIN_BIN 20 /* 625 Гц */
#define PILOT_MAX_BIN 44 /* 1375 Гц */

// Коэффициенты для квадратичной шкалы: 11.0f ≈ 3.3х в линейной, 7.5f ≈ 2.7х в линейной
#define PILOT_THRESH_STRICT   11
#define PILOT_THRESH_RELAXED  7

#define COARSE_WIDTH	32

typedef struct {
#if (COARSE_WIDTH==16)
    uint16_t phase_acc;
    uint16_t phase_inc;
#else
    uint32_t phase_acc;
    uint32_t phase_inc;
#endif
    short *sine_lut;
} coarse_mixer_t;

typedef struct {
    cplx_f32 history[FIR_TAPS];
    cplx_f32 historyx[FIR_TAPX];
    int idx;
} fir_filter_t;

void fir_filter_init(fir_filter_t *fir);
void fir_filter_process(fir_filter_t *fir, const cplx_f32 *in_sample, cplx_f32 *out_sample);
void fir_filter_complex_process(fir_filter_t *fir, const cplx_f32 *in_sample, cplx_f32 *out_sample);


// --- 1. HEAVY PC EXTENSION (Double Precision, 1024-point Float FFT) ---
// Used as the absolute truth model to evaluate degradation
void fft_heavy_1024(const cplx_f32 *in_samples, float *out_magnitude);

// --- 2. LIGHTWEIGHT MCU PRODUCTION (256-point Fixed-Point Q15 FFT) ---
// Optimized for RISC-V. Inputs are fed as raw Q15 fractions (scaled down to prevent overflow)
// Output contains integer squared magnitudes for energy detection
void fft_light_fixed256(const int16_t *in_i, const int16_t *in_q, uint32_t *out_sq_magnitude);

// Helper to initialize fixed-point twiddle factors and Hamming window
void fft_init_tables(void);

float check_spectrum_for_pilot(const float *fft_magnitude);
float check_mcu_spectrum_for_pilot(const uint32_t *out_sq_magnitude);
void coarse_mixer_process(coarse_mixer_t *mixer, const cplx_f32 *in_sample, cplx_f32 *out_sample);
void coarse_mixer_init(coarse_mixer_t *mixer, float freq_error, short *sine_table_ptr);




#endif // FFT_SYNC_H
