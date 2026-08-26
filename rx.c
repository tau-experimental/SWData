#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#include "rx.h"

// Глобальные контексты каскадов приемника
static mixer_stage_t rx_mixer;
 bpf_stage_t   rx_bpf_bank[NUM_DATA_TONES];
static pd_stage_t    rx_pd_bank[NUM_DATA_TONES];

// Текущие глобальные оценки частотных ошибок по каналам
float ch_cfo_estimates[NUM_DATA_TONES];
// Статическое состояние ФНЧ петли АПЧ для сохранения истории между сэмплами
float debug_smooth_cfo_hz, smooth_cfo_rad = 0.0f;
static float         smooth_cfo_hz_accumulator = 0.0f;

// ============================================================================
// КАСКАД 1: ВХОДНОЙ СМЕСИТЕЛЬ С ПОДСТРОЙКОЙ (Heterodyne & Mixer)
// ============================================================================
void stage1_mixer(const complex_f *input, complex_f *output) {
    // Вычисляем комплексный коэффициент гетеродина e^(-j * phase)
    // Знак минус, чтобы вращать спектр навстречу дрейфу частоты
    float cos_h = cosf(rx_mixer.phase);
    float sin_h = sinf(rx_mixer.phase);

    // Комплексное умножение: Output = Input * e^(-j * phase)
    output->re = input->re * cos_h + input->im * sin_h;
    output->im = input->im * cos_h - input->re * sin_h;

    // Обновляем фазу гетеродина с учетом накопленной петлей АПЧ коррекции частоты
    rx_mixer.phase += rx_mixer.freq_correction;

    // Ограничение фазы в границах [-PI, PI]
    if (rx_mixer.phase >  PI_F) rx_mixer.phase -= 2.0f * PI_F;
    if (rx_mixer.phase < -PI_F) rx_mixer.phase += 2.0f * PI_F;
}

// ============================================================================
// КАСКАД 2: БАНК ПОЛОСОВЫХ ФИЛЬТРОВ (BPF Bank)
// ============================================================================
void rx_pipeline_init(void) {
    rx_mixer.phase = 0.0f;
    rx_mixer.freq_correction = 0.0f;
    smooth_cfo_hz_accumulator = 0.0f;

    for (int t = 0; t < NUM_DATA_TONES; t++) {
        float tone_freq = data_tones[t];
        bpf_stage_t *bpf = &rx_bpf_bank[t];

        bpf->freq_target = tone_freq;
        bpf->R_coeff = 0.9804f; // Наша целевая полоса 50 Гц
        bpf->K_coeff = 2.0f * bpf->R_coeff * cosf(2.0f * PI_F * tone_freq / FS);

        // Вычисляем точный нормирующий коэффициент для 0 дБ на резонансе
        float w0 = 2.0f * PI_F * tone_freq / FS;
        bpf->gain_scale = (1.0f - bpf->R_coeff * bpf->R_coeff) * sinf(w0) * sqrtf(0.5f); // корректируем усиление

        // Если sin(w0) слишком мал или для простоты, можно использовать верхнюю оценку:
        // bpf->gain_scale = (1.0f - bpf->R_coeff) * 0.5f;
        // Но тригонометрическая формула выше дает идеальный ноль децибел для комплексного синуса!

        bpf->i_a1 = bpf->i_a2 = bpf->q_a1 = bpf->q_a2 =
        		bpf->i_b1 = bpf->i_b2 = bpf->q_b1 = bpf->q_b2 = 0.0f;

        bpf->y_curr.re = 0.0f; bpf->y_curr.im = 0.0f;
        bpf->y_prev.re = 0.0f; bpf->y_prev.im = 0.0f;

        ch_cfo_estimates[t] = 0.0f;
    }
}

// В функции rx_pipeline_init сброс регистров теперь выглядит так:
// bpf->i_a1 = 0; bpf->i_a2 = 0; bpf->q_a1 = 0; bpf->q_a2 = 0;
// bpf->i_b1 = 0; bpf->i_b2 = 0; bpf->q_b1 = 0; bpf->q_b2 = 0;
// Для идеальной компенсации +6 дБ комплексного каскада, немного уменьшим gain_scale:
// bpf->gain_scale = ((1.0f - bpf->R_coeff * bpf->R_coeff) * sinf(w0)) * 0.7f;

void stage2_bpf_filter(int tone_idx, const complex_f *input) {
    bpf_stage_t *bpf = &rx_bpf_bank[tone_idx];

    bpf->y_prev = bpf->y_curr;

    // --- ЗВЕНО 1 (Каскад А) ---
    float in_scaled_re = input->re * (bpf->gain_scale * bpf->gain_scale);
    float in_scaled_im = input->im * (bpf->gain_scale * bpf->gain_scale);

    // Фильтруем I (Звено 1)
    float i_v0_a = in_scaled_re + bpf->K_coeff * bpf->i_a1 - (bpf->R_coeff * bpf->R_coeff) * bpf->i_a2;
    float out_a_re = i_v0_a - bpf->i_a2;
    bpf->i_a2 = bpf->i_a1;
    bpf->i_a1 = i_v0_a;

    // Фильтруем Q (Звено 1)
    float q_v0_a = in_scaled_im + bpf->K_coeff * bpf->q_a1 - (bpf->R_coeff * bpf->R_coeff) * bpf->q_a2;
    float out_a_im = q_v0_a - bpf->q_a2;
    bpf->q_a2 = bpf->q_a1;
    bpf->q_a1 = q_v0_a;

    // --- ЗВЕНО 2 (Каскад Б) ---
    // Подаем выход Звена 1 на вход Звену 2

    // Фильтруем I (Звено 2)
    float i_v0_b = out_a_re + bpf->K_coeff * bpf->i_b1 - (bpf->R_coeff * bpf->R_coeff) * bpf->i_b2;
    bpf->y_curr.re = i_v0_b - bpf->i_b2;
    bpf->i_b2 = bpf->i_b1;
    bpf->i_b1 = i_v0_b;

    // Фильтруем Q (Звено 2)
    float q_v0_b = out_a_im + bpf->K_coeff * bpf->q_b1 - (bpf->R_coeff * bpf->R_coeff) * bpf->q_b2;
    bpf->y_curr.im = q_v0_b - bpf->q_b2;
    bpf->q_b2 = bpf->q_b1;
    bpf->q_b1 = q_v0_b;
}

// ============================================================================
// КАСКАД 3: ОЦЕНКА СДВИГА ЧАСТОТЫ (Frequency Shift Assessment & PLL)
// ============================================================================
float last_total_theta[NUM_DATA_TONES] = {0};
float phase_stability_acc[NUM_DATA_TONES] = {0};
void stage3_frequency_assessment(void) {
    float sum_cfo_rad = 0.0f;
    int active_channels_count = 0;

    for (int t = 0; t < NUM_DATA_TONES; t++) {
        bpf_stage_t *bpf = &rx_bpf_bank[t];
        float amplitude = sqrtf(bpf->y_curr.re * bpf->y_curr.re + bpf->y_curr.im * bpf->y_curr.im);

        // 1. Измеряем автокорреляцию
        float d_re = bpf->y_curr.re * bpf->y_prev.re + bpf->y_curr.im * bpf->y_prev.im;
        float d_im = bpf->y_curr.im * bpf->y_prev.re - bpf->y_curr.re * bpf->y_prev.im;
        float total_theta = atan2f(d_im, d_re);

        // 2. Детектор дисперсии фазы (критерий когерентности)
        static float last_total_theta[NUM_DATA_TONES] = {0};
        static float phase_stability_acc[NUM_DATA_TONES] = {0};

        float phase_acceleration = fabsf(total_theta - last_total_theta[t]);
        if (phase_acceleration > PI_F) phase_acceleration = 2.0f * PI_F - phase_acceleration;
        last_total_theta[t] = total_theta;

        phase_stability_acc[t] = 0.97f * phase_stability_acc[t] + 0.03f * phase_acceleration;

        // Вердикт: сигнал структурный и мощный
        bool is_signal_present = (phase_stability_acc[t] < 0.25f) && (amplitude > 0.15f);

        if (is_signal_present) {
            float w0 = 2.0f * PI_F * bpf->freq_target / FS;
            float delta_theta = total_theta - w0;

            while (delta_theta >  PI_F) delta_theta -= 2.0f * PI_F;
            while (delta_theta < -PI_F) delta_theta += 2.0f * PI_F;

            ch_cfo_estimates[t] = delta_theta * (FS / (2.0f * PI_F));

            sum_cfo_rad += delta_theta;
            active_channels_count++;
        } else {
            ch_cfo_estimates[t] = 0.0f;
        }
    }

    // --- ИНТЕГРАЦИЯ И ФИЛЬТРАЦИЯ СДВИГА ЧАСТОТЫ ---
    float current_avg_error_hz = 0.0f;
    if (active_channels_count > 0) {
        float avg_cfo_rad = sum_cfo_rad / (float)active_channels_count;
        current_avg_error_hz = avg_cfo_rad * (FS / (2.0f * PI_F));
    }

    // ФНЧ петли: теперь smooth_cfo_hz_accumulator железобетонно сохраняет историю!
    float alpha_loop_lpf = 0.01f;
    smooth_cfo_hz_accumulator = (1.0f - alpha_loop_lpf) * smooth_cfo_hz_accumulator + alpha_loop_lpf * current_avg_error_hz;

    // Вывод в глобальную переменную отладки
    debug_smooth_cfo_hz = smooth_cfo_hz_accumulator;

    // --- ПЕТЛЯ АПЧ ---
    float k_loop = 0.000f; // Пока держим статику (0.000) для проверки Герц
    float smooth_cfo_rad = smooth_cfo_hz_accumulator * ((2.0f * PI_F) / FS);
    rx_mixer.freq_correction += k_loop * smooth_cfo_rad;
}
// ============================================================================
// КАСКАД 4: ЛИНЕЙКА ФАЗОВЫХ ДЕТЕКТОРОВ (Phase Detectors)
// ============================================================================
void stage4_phase_detector(int tone_idx, const complex_f *bpf_output) {
    pd_stage_t *pd = &rx_pd_bank[tone_idx];

    float mag_sq = bpf_output->re * bpf_output->re + bpf_output->im * bpf_output->im;
    if (mag_sq > 1e-4f) {
        // Извлекаем абсолютную текущую фазу очищенной синусоиды поднесущей
        float current_phase = atan2f(bpf_output->im, bpf_output->re);

        // Считаем скачок фазы между соседними сэмплами
        float phase_jump = fabsf(current_phase - pd->last_phase);
        if (phase_jump > PI_F) phase_jump = 2.0f * PI_F - phase_jump;

        pd->last_phase = current_phase;

        // Пороговый детектор информационного фронта (для DPSK скачки обычно 90 или 180 градусов)
        // Порог в 0.5 радиана (~30 градусов) уверенно поймает скачок, отфильтрованный BPF
        if (phase_jump > 0.5f) {
            pd->is_switching = true;
            pd->hold_counter = HOLD_TIME_SAMPLES; // Запуск Hold-таймера удержания флага
        }
    }

    // Обслуживание Hold-таймера удержания состояния подозрения
    if (pd->hold_counter > 0) {
        pd->hold_counter--;
        if (pd->hold_counter == 0) {
            pd->is_switching = false;
        }
    }
}

// ============================================================================
// ЦЕНТРАЛЬНЫЙ ДИСПЕТЧЕР КОНВЕЙЕРА (Вызывается на каждый входящий сэмпл)
// ============================================================================
void rx_pipeline_process_sample(const complex_f *raw_sample) {
    complex_f mixed_sample;

    // Каскад 1: Смеситель АПЧ
    //stage1_mixer(raw_sample, &mixed_sample);

    // Каскад 2: Фильтрация (накапливаем y_curr внутри банка)
    for (int t = 0; t < NUM_DATA_TONES; t++) {
        stage2_bpf_filter(t, &mixed_sample);
    }

    // Каскад 3: Оценка частоты по вашей схеме (сводит freq.delta)
    stage3_frequency_assessment();

    // Каскад 4: Детекторы фазы (анализируют стабильный bpf_bank[t].y_curr)
    //for (int t = 0; t < NUM_DATA_TONES; t++) {  stage4_phase_detector(t, &rx_bpf_bank[t].y_curr); }
}

// Геттеры для отладки и тестов из main.c
float rx_debug_get_ch_cfo(int tone_idx) { return ch_cfo_estimates[tone_idx]; }
float rx_debug_get_mixer_correction_hz(void) { return rx_mixer.freq_correction * (FS / (2.0f * PI_F)); }
bool  rx_debug_get_ch_switching(int tone_idx) { return rx_pd_bank[tone_idx].is_switching; }
