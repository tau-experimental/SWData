#ifndef FFT_SYNC_H
#define FFT_SYNC_H

#include "wav_io.h" // For cplx_f32 type
#include <stdint.h>

// --- 1. HEAVY PC EXTENSION (Double Precision, 1024-point Float FFT) ---
// Used as the absolute truth model to evaluate degradation
void fft_heavy_1024(const cplx_f32 *in_samples, float *out_magnitude);

// --- 2. LIGHTWEIGHT MCU PRODUCTION (256-point Fixed-Point Q15 FFT) ---
// Optimized for RISC-V. Inputs are fed as raw Q15 fractions (scaled down to prevent overflow)
// Output contains integer squared magnitudes for energy detection
void fft_light_fixed256(const int16_t *in_i, const int16_t *in_q, uint32_t *out_sq_magnitude);

// Helper to initialize fixed-point twiddle factors and Hamming window
void fft_init_tables(void);

#endif // FFT_SYNC_H
