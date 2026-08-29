#include "fft_sync.h"
#include <math.h>
#include <string.h>

// Global lookup tables for the Lightweight MCU 256-FFT
static int16_t mcu_hamming_q15[256];
static int16_t twiddle_cos_q15[128]; // Only need N/2 factors
static int16_t twiddle_sin_q15[128];
static uint8_t bit_reverse_256[256];

// Pre-calculate tables (Runs once at startup)
void fft_init_tables(void) {
    // 1. Generate Hamming Window: w(n) = 0.54 - 0.46 * cos(2*pi*n / (N-1))
    for (int i = 0; i < 256; i++) {
        double w = 0.54 - 0.46 * cos((2.0 * M_PI * i) / 255.0);
        mcu_hamming_q15[i] = (int16_t)(w * 32767.0);
    }

    // 2. Generate Radix-2 Twiddle Factors
    for (int i = 0; i < 128; i++) {
        double angle = (2.0 * M_PI * i) / 256.0;
        twiddle_cos_q15[i] = (int16_t)(cos(angle) * 32767.0);
        twiddle_sin_q15[i] = (int16_t)(-sin(angle) * 32767.0); // Negative for forward FFT
    }

    // 3. Generate Bit Reversal Lookup Table for 256 points (8 bits)
    for (int i = 0; i < 256; i++) {
        unsigned int rev = 0;
        unsigned int val = i;
        for (int b = 0; b < 8; b++) {
            rev = (rev << 1) | (val & 1);
            val >>= 1;
        }
        bit_reverse_256[i] = (uint8_t)rev;
    }
}

// ============================================================================
// 1. HEAVY PC EXTENSION (1024-Point Float FFT with Hamming Window)
// ============================================================================
void fft_heavy_1024(const cplx_f32 *in_samples, float *out_magnitude) {
    static float rex[1024], imx[1024];

    // Apply Hamming window and load data
    for (int i = 0; i < 1024; i++) {
        float w = 0.54f - 0.46f * cosf((2.0f * M_PI * i) / 1023.0f);
        rex[i] = in_samples[i].re * w;
        imx[i] = in_samples[i].im * w;
    }

    // Standard Radix-2 In-place Float FFT (Cooley-Tukey)
    int i, j, k, l, le, le1, ip;
    float tr, ti, ur, ui, wr, wi;

    int n = 1024;
    int m = 10; // 2^10 = 1024

    // Bit reversal
    j = 0;
    for (i = 0; i < n - 1; i++) {
        if (i < j) {
            tr = rex[j]; ti = imx[j];
            rex[j] = rex[i]; imx[j] = imx[i];
            rex[i] = tr; imx[i] = ti;
        }
        k = n / 2;
        while (k <= j) { j -= k; k /= 2; }
        j += k;
    }

    // FFT Loops
    for (l = 1; l <= m; l++) {
        le = 1 << l;
        le1 = le / 2;
        ur = 1.0f; ui = 0.0f;
        wr = cosf(M_PI / le1);
        wi = -sinf(M_PI / le1);
        for (j = 0; j < le1; j++) {
            for (i = j; i < n; i += le) {
                ip = i + le1;
                tr = rex[ip] * ur - imx[ip] * ui;
                ti = rex[ip] * ui + imx[ip] * ur;
                rex[ip] = rex[i] - tr;
                imx[ip] = imx[i] - ti;
                rex[i] += tr;
                imx[i] += ti;
            }
            tr = ur * wr - ui * wi;
            ui = ur * wi + ui * wr;
            ur = tr;
        }
    }

    // Calculate Magnitudes
    for (i = 0; i < 1024; i++) {
        out_magnitude[i] = sqrtf(rex[i]*rex[i] + imx[i]*imx[i]);
    }
}

// ============================================================================
// 2. LIGHTWEIGHT MCU PRODUCTION (256-Point Fixed-Point Q15 Radix-2 FFT)
// ============================================================================
void fft_light_fixed256(const int16_t *in_i, const int16_t *in_q, uint32_t *out_sq_magnitude) {
    // Local working buffers to perform in-place bit-shifting operations
    static int16_t fr[256];
    static int16_t fi[256];

    // Stage 1: Load, apply Q15 Hamming window, and Bit-Reverse simultaneously
    // To prevent fixed-point addition overflow during butterfly stages,
    // we pre-scale input samples down by shifting right by 4 bits (div by 16)
    for (int i = 0; i < 256; i++) {
        int8_t rev_idx = bit_reverse_256[i];

        // Fixed point fractional multiplication: (A * B) >> 15
        int32_t i_win = ((int32_t)in_i[i] * mcu_hamming_q15[i]) >> 15;
        int32_t q_win = ((int32_t)in_q[i] * mcu_hamming_q15[i]) >> 15;

        fr[rev_idx] = (int16_t)(i_win >> 4);
        fi[rev_idx] = (int16_t)(q_win >> 4);
    }

    // Stage 2: Fixed-point Cooley-Tukey Butterflies (8 stages for N=256)
    int stage, step, group, pair;
    int log2_n = 8;

    for (stage = 1; stage <= log2_n; stage++) {
        int m = 1 << stage;
        int m2 = m >> 1;
        int twiddle_stride = 128 >> (stage - 1);

        for (group = 0; group < 256; group += m) {
            for (pair = 0; pair < m2; pair++) {
                // Fetch pre-calculated Q15 twiddle factors
                int16_t wr = twiddle_cos_q15[pair * twiddle_stride];
                int16_t wi = twiddle_sin_q15[pair * twiddle_stride];

                int match = group + pair + m2;
                int base  = group + pair;

                // Q15 Complex multiplication: T = Target * Twiddle
                // tr = (fr*wr - fi*wi), ti = (fr*wi + fi*wr)
                int32_t tr = ((int32_t)fr[match] * wr - (int32_t)fi[match] * wi) >> 15;
                int32_t ti = ((int32_t)fr[match] * wi + (int32_t)fi[match] * wr) >> 15;

                // Butterfly additions/subtractions (safe from overflow due to initial downscaling)
                fr[match] = fr[base] - (int16_t)tr;
                fi[match] = fi[base] - (int16_t)ti;
                fr[base] += (int16_t)tr;
                fi[base] += (int16_t)ti;
            }
        }
    }

    // Stage 3: Calculate Squared Magnitudes (Saves CPU cycles by omitting sqrt)
    for (int i = 0; i < 256; i++) {
        int32_t r = fr[i];
        int32_t j = fi[i];
        out_sq_magnitude[i] = (uint32_t)(r*r + j*j);
    }
}
