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
        uint8_t rev_idx = bit_reverse_256[i];

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

// Возвращает расстройку в Гц, если пилот найден, или -999.0f, если в эфире только шум
// Сканирует целочисленный спектр.
float check_mcu_spectrum_for_pilot(const uint32_t *out_sq_magnitude) {
    int max_bin = -1;
    uint64_t max_sq_amp = 0;
    uint64_t noise_sq_sum = 0;
    int noise_count = 0;

    // 1. Сканируем спектр (до частоты Найквиста - 128 бин)
    for (int bin = 0; bin < 128; bin++) {
        if (bin >= 29 && bin <= 35) {
            if (out_sq_magnitude[bin] > max_sq_amp) {
                max_sq_amp = out_sq_magnitude[bin];
                max_bin = bin;
            }
        } else {
            // Суммируем квадраты энергий шумовых бинов
            noise_sq_sum += out_sq_magnitude[bin];
            noise_count++;
        }
    }

    if (max_bin == -1 || noise_count == 0) return -999.0f;

    uint64_t average_sq_noise = noise_sq_sum / noise_count;

    // ЗАЩИТА ОТ МЕРТВОЙ ТИШИНЫ: если энергии в пике вообще нет, игнорируем
    if (max_sq_amp < 10) return -999.0f;

    // Спектральный критерий в квадратичной шкале:
    // Так как амплитуда возведена в квадрат, отношение SNR тоже возводится в квадрат.
    // Превышение среднего шума в 3.5 раза в линейной шкале — это 3.5^2 ≈ 12.25 раз в квадратичной!
    // Используем строгое целочисленное сравнение без деления:
    if (max_sq_amp > (average_sq_noise * 13)) {
        float found_freq = (float)max_bin * BIN_RESOLUTION;
        float freq_error = found_freq - 1000.0f; // Вычисляем сдвиг до ПЧ 1000 Гц
        return freq_error;
    }

    return -999.0f;
}

void coarse_mixer_init(coarse_mixer_t *mixer, float freq_error, short *sine_table_ptr) {
    mixer->sine_lut = sine_table_ptr;
    mixer->phase_acc = 0;
    uint16_t FTW = (unsigned short)((freq_error * 65536.0f) / 8000.0f);
    printf ("Coarse Mixer FTW: %u\n", FTW);

    // Переводим частоту коррекции в шаг DDS FTW
    mixer->phase_inc = FTW;
}

void coarse_mixer_process(coarse_mixer_t *mixer, const cplx_f32 *in_sample, cplx_f32 *out_sample) {
    unsigned char sin_idx = (unsigned char)(mixer->phase_acc >> 8);
    unsigned char cos_idx = (unsigned char)((sin_idx + 64) & 0xFF);

    float c = (float)mixer->sine_lut[cos_idx] / 32000.0f;
    float s = (float)mixer->sine_lut[sin_idx] / 32000.0f;

    // Комплексное перемножение: сдвигаем спектр входного сигнала
    out_sample->re = in_sample->re * c + in_sample->im * s;
    out_sample->im = in_sample->im * c - in_sample->re * s;

    mixer->phase_acc = (unsigned short)(mixer->phase_acc + mixer->phase_inc);
}

// Коэффициенты нормированы (в формате float для модели, легко переводятся в Q15 для CH32V307)
/*static const float fir_coeffs[FIR_TAPS] = {
    -0.0012f, -0.0034f, -0.0051f, -0.0042f,  0.0011f,  0.0112f,  0.0234f,  0.0331f,
     0.0352f,  0.0261f,  0.0041f, -0.0282f, -0.0631f, -0.0924f, -0.1102f, -0.1164f,
    -0.1102f, -0.0924f, -0.0631f, -0.0282f,  0.0041f,  0.0261f,  0.0352f,  0.0331f,
     0.0234f,  0.0112f,  0.0011f, -0.0042f, -0.0051f, -0.0034f, -0.0012f,  0.0000f
};*/

// Новые коэффициенты полосового фильтра 900..1100 Гц (ПЧ 1000 Гц)
static const float fir_coeffs[FIR_TAPS] = {
     0.0031f, -0.0021f, -0.0084f,  0.0042f,  0.0182f, -0.0091f, -0.0364f,  0.0182f,
     0.0651f, -0.0382f, -0.1112f,  0.0812f,  0.2241f, -0.2112f, -0.4502f,  0.4214f,
     0.4214f, -0.4502f, -0.2112f,  0.2241f,  0.0812f, -0.1112f, -0.0382f,  0.0651f,
     0.0182f, -0.0364f, -0.0091f,  0.0182f,  0.0042f, -0.0084f, -0.0021f,  0.0031f
};

static const cplx_f32 fir_coeffs_complex[FIR_TAPX] = {
	    { 0.0001,  0.0000}, { 0.0002,  0.0002}, { 0.0000,  0.0006}, {-0.0007, -0.0007}, {-0.0016,  0.0000}, { 0.0018,  0.0018}, { 0.0000,  0.0045}, {-0.0049, -0.0049},
	    {-0.0089,  0.0000}, { 0.0094,  0.0094}, { 0.0000,  0.0205}, {-0.0210, -0.0210}, {-0.0354,  0.0000}, { 0.0355,  0.0355}, { 0.0000,  0.0763}, {-0.0744, -0.0744},
	    {-0.1287,  0.0000}, { 0.1287,  0.1287}, { 0.0000,  0.2974}, {-0.3048, -0.3048}, {-0.5976,  0.0000}, { 0.6300,  0.6300}, { 0.0000,  1.7450}, {-2.2220, -2.2220},
	    {-5.7360,  0.0000}, { 8.8850,  8.8850}, { 0.0000, 37.6620},{-85.3520,-85.3520},{-315.652,  0.0000},{838.225,838.225}, { 0.0000,4742.65},{-15220.5,-15220.5},
	    {15220.5, -15220.5}, { 0.0000, -4742.65},{-838.225,838.225}, {-315.652,  0.0000},{-85.3520,85.3520}, { 0.0000, -37.6620},{ 8.8850, -8.8850}, {-5.7360,  0.0000},
	    {-2.2220,  2.2220}, { 0.0000,  1.7450}, { 0.6300, -0.6300}, {-0.5976,  0.0000},{-0.3048,  0.3048}, { 0.0000, -0.2974}, { 0.1287, -0.1287}, {-0.1287,  0.0000},
	    {-0.0744,  0.0744}, { 0.0000, -0.0763}, { 0.0355, -0.0355}, {-0.0354,  0.0000},{-0.0210,  0.0210}, { 0.0000, -0.0205}, { 0.0094, -0.0094}, {-0.0089,  0.0000},
	    {-0.0049,  0.0049}, { 0.0000, -0.0045}, { 0.0018, -0.0018}, {-0.0016,  0.0000},{-0.0007,  0.0007}, { 0.0000, -0.0006}, { 0.0002, -0.0002}, { 0.0001,  0.0000}
	};

void fir_filter_init(fir_filter_t *fir) {
    fir->idx = 0;
    for (int i = 0; i < FIR_TAPS; i++) {
        fir->history[i].re = 0.0f;
        fir->history[i].im = 0.0f;
    }
}

void fir_filter_process(fir_filter_t *fir, const cplx_f32 *in_sample, cplx_f32 *out_sample) {
    fir->history[fir->idx] = *in_sample;

    cplx_f32 acc = {0.0f, 0.0f};
    int tap_idx = fir->idx;

    for (int i = 0; i < FIR_TAPS; i++) {
        acc.re += fir->history[tap_idx].re * fir_coeffs[i];
        acc.im += fir->history[tap_idx].im * fir_coeffs[i];

        tap_idx--;
        if (tap_idx < 0) tap_idx = FIR_TAPS - 1;
    }

    *out_sample = acc;

    fir->idx++;
    if (fir->idx >= FIR_TAPS) fir->idx = 0;
}

void fir_filter_complex_process(fir_filter_t *fir, const cplx_f32 *in_sample, cplx_f32 *out_sample) {
    fir->historyx[fir->idx] = *in_sample;

    cplx_f32 acc = {0.0f, 0.0f};
    int tap_idx = fir->idx;

    for (int i = 0; i < FIR_TAPX; i++) {
        float sr = fir->historyx[tap_idx].re;
        float si = fir->historyx[tap_idx].im;
        float cr = fir_coeffs_complex[i].re;
        float ci = fir_coeffs_complex[i].im;

        // Комплексное умножение: acc += sample * coeff
        acc.re += (sr * cr + si * ci);
        acc.im += (sr * ci - si * cr);

        tap_idx--;
        if (tap_idx < 0) tap_idx = FIR_TAPX - 1;
    }

    acc.im *= 0.001f;
    acc.re *= 0.001f;

    *out_sample = acc;

    fir->idx++;
    if (fir->idx >= FIR_TAPS) fir->idx = 0;
}

