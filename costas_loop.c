#include "modulator.h" // Используем общую каноническую таблицу sin_lut_256

#include "costas_loop.h"

static inline int16_t lut_sin(uint16_t phase) { return sin_lut_256[phase >> 8]; }
static inline int16_t lut_cos(uint16_t phase) { uint8_t idx = (phase >> 8) + 64; return sin_lut_256[idx]; }

void costas_init(costas_loop_t *loop, float freq_offset_hz) {
    // Вычисляем реальную частоту несущей в эфире
    float true_carrier_hz = 1000.0f + freq_offset_hz; // Например, 955.078 Гц

    // Переводим частоту в 16-битный шаг фазы DDS для Fs = 8000 Гц
    // FTW = round(F * 65536 / 8000) = F * 8.192
    uint16_t ftw = (uint16_t)roundf(true_carrier_hz * 8.192f);

    loop->phase_acc = 0;
    loop->phase_inc = ftw;
    loop->base_phase_inc = ftw;

    loop->sum_i = 0;
    loop->sum_q = 0;
    loop->sample_counter = 0;
    loop->integrator = 0;

    // Настройка полосы петли (Loop Bandwidth) под полярный КВ-канал.
    // Коэффициенты подобраны так, чтобы петля была узкой (фильтровала шум),
    // но успевала за ионосферным дрейфом. Умножение заменяется сдвигом вправо.
    loop->shift_p = 7;   // Kp = 2^(-7)  = 0.0078
    loop->shift_i = 11;  // Ki = 2^(-11) = 0.00048
}

void advanced_costas_init(advanced_costas_t *loop, float freq_offset_hz) {
    uint16_t ftw = (uint16_t)roundf((1000.0f + freq_offset_hz) * 8.192f);
    loop->phase_acc = 0;
    loop->phase_inc = ftw;
    loop->base_phase_inc = ftw;

    loop->sum_i = 0; loop->sum_q = 0; loop->sum_energy = 0;
    loop->sample_counter = 0;
    loop->prev_sum_i = 0; loop->prev_sum_q = 0;
    loop->integrator = 0;

    loop->mode = COSTAS_MODE_ACQUISITION; // Стартуем в режиме агрессивного захвата
    loop->lock_metric = 0;
}

/**
 * @brief Поотсчетная работа петли Костаса 8 кГц
 * @param in_i, in_q   Входной зашумленный отсчет из WAV (формат Q15, int16_t)
 * @param bit_strobe   Указатель-флаг: станет равным 1 строго в момент окончания символа
 * @param out_decision Демодулированный знак символа (BPSK: +1 или -1), когда bit_strobe == 1
 */
void costas_process_sample(costas_loop_t *loop, int16_t in_i, int16_t in_q,
                           int *bit_strobe, int *out_decision) {
    *bit_strobe = 0;

    // 1. Извлекаем синус и косинус из вашей таблицы sin_lut_256
    // Старший байт 16-битной фазы дает индекс от 0 до 255
    uint8_t lut_idx = (uint8_t)(loop->phase_acc >> 8);
    int16_t cos_n = sin_lut_256[(lut_idx + 64) & 0xFF]; // Косинус со сдвигом на 90 градусов
    int16_t sin_n = sin_lut_256[lut_idx];

    // 2. Демодулирующее комплексное перемножение (Вращение вектора фазы обратно)
    // Формат Q15 fractional multiplication: (A * B) >> 15
    int32_t demod_i = ((int32_t)in_i * cos_n + (int32_t)in_q * sin_n) >> 15;
    int32_t demod_q = ((int32_t)in_q * cos_n - (int32_t)in_i * sin_n) >> 15;

    // 3. Накопление энергии внутри символа (Integrate)
    loop->sum_i += demod_i;
    loop->sum_q += demod_q;

    // Шаг фазы локального генератора на следующий отсчет
    loop->phase_acc += loop->phase_inc;

    // 4. Точка сброса (Dump) и коррекции — наступил конец символа (256 отсчетов)
    loop->sample_counter++;
    if (loop->sample_counter >= 256) {
        loop->sample_counter = 0;
        *bit_strobe = 1; // Сигнализируем конвейеру: символ готов!

        // Жесткое решение (Slicer) по знаку синфазного канала для BPSK
        int sign_i = (loop->sum_i >= 0) ? 1 : -1;
        *out_decision = sign_i;

        // 5. Decision-Directed дискриминатор фазовой ошибки петли Костаса.
        // Математика: error = Q_sum * sign(I_sum)
        // Умножение на знак (sign_i) убирает BPSK-модуляцию (0/180 градусов),
        // превращая дискриминатор в чистый детектор ошибки фазы несущей.
        int32_t phase_error = loop->sum_q * sign_i;

        // Нормируем ошибку фазы, чтобы защитить интеграторы от переполнения
        phase_error >>= 8;

        // 6. Петлевой PI-фильтр (Рассчитывает подстройку частоты NCO)
        // Интегральная ветвь (компенсирует ионосферный дрейф частоты)
        loop->integrator += (phase_error >> loop->shift_i);

        // Пропорциональная ветвь + Интегральная ветвь
        int32_t loop_filter_output = (phase_error >> loop->shift_p) + loop->integrator;

        // 7. Коррекция шага частоты DDS на следующий символ
        // Ограничиваем максимальную полосу удержания петли (например, в пределах ±40 Гц)
        // 40 Гц * 8.192 = ±327 отсчетов FTW
        if (loop_filter_output > 327)  loop_filter_output = 327;
        if (loop_filter_output < -327) loop_filter_output = -327;

        loop->phase_inc = (uint16_t)(loop->base_phase_inc + loop_filter_output);

        // Сбрасываем (Dump) интеграторы символа для следующего такта связи
        loop->sum_i = 0;
        loop->sum_q = 0;
    }
}
#include <stdio.h>
void advanced_costas_process(advanced_costas_t *loop, int16_t in_i, int16_t in_q,
                             int *bit_strobe, int *out_decision, int *is_locked) {
    *bit_strobe = 0;

    // 1. Демодуляция гетеродином
    uint8_t lut_idx = (uint8_t)(loop->phase_acc >> 8);
    int16_t cos_n = sin_lut_256[(lut_idx + 64) & 0xFF];
    int16_t sin_n = sin_lut_256[lut_idx];

    int32_t demod_i = ((int32_t)in_i * cos_n + (int32_t)in_q * sin_n) >> 15;
    int32_t demod_q = ((int32_t)in_q * cos_n - (int32_t)in_i * sin_n) >> 15;

    loop->sum_i += demod_i;
    loop->sum_q += demod_q;

    // Копим полную энергию отсчетов (масштабируем, чтобы избежать переполнения)
    //loop->sum_energy += (demod_i * demod_i + demod_q * demod_q) >> 10;
    loop->sum_energy += (demod_i * demod_i + demod_q * demod_q);


    loop->phase_acc += loop->phase_inc;
    loop->sample_counter++;


    // Выполняется строго раз за символ (31.25 Гц)
    if (loop->sample_counter >= 256) {
        loop->sample_counter = 0;
        *bit_strobe = 1;
        int sign_i = (loop->sum_i >= 0) ? 1 : -1;

        // 1. Вычисляем когерентную энергию (квадрат модуля интегрированного вектора)
        double coherent_energy = (double)loop->sum_i * loop->sum_i + (double)loop->sum_q * loop->sum_q;

        // 2. Вычисляем общую энергию символа правильным математическим способом.
        // Вместо поотсчетного накопления sum_energy, воспользуемся тем, что при
        // когерентном захвате (на любой оси) coherent_energy стремится к максимуму,
        // а в чистой тишине/шуме из-за деструктивной интерференции 256 отсчетов шума
        // coherent_energy падает в 256 раз относительно полной мощности шума.

        // Чтобы получить пуленепробиваемый безразмерный коэффициент Lock Metric,
        // мы берем амплитудные суммы по осям I и Q.
        double abs_i = fabs((double)loop->sum_i);
        double abs_q = fabs((double)loop->sum_q);

        // Метрика фазового замка Костаса (Costas Lock Metric):
        // На BPSK/пилоте сигнал всегда схлопывается ЛИБО на ось I, ЛИБО на ось Q.
        // Если он лег на I: abs_i >> abs_q. Если на Q: abs_q >> abs_i.
        // Значит, их разность по модулю |abs_i - abs_q|, деленная на их сумму,
        // будет строго равна 1.0 при идеальном захвате В ЛЮБОЙ ФАЗЕ (0 или 90 градусов)!
        // А в тишине или хаотичном шуме abs_i и abs_q будут строго равны, давая чистый 0.0.
        double instant_lock = 0.0;
        double amplitude_sum = abs_i + abs_q;

        if (amplitude_sum > 0.001) {
            instant_lock = fabs(abs_i - abs_q) / amplitude_sum;
        }

        // 3. Быстрое сглаживание (альфа-фильтр 0.5)
        static double smoothed_lock = 0.0;
        smoothed_lock = smoothed_lock + 0.5 * (instant_lock - smoothed_lock);

        // Масштабируем в 0..100 для красивого вывода
        loop->lock_metric = (int32_t)(smoothed_lock * 100.0);

        // 4. Двухпороговый автомат режимов (Гистерезис)
        if (loop->mode == COSTAS_MODE_ACQUISITION) {
            if (loop->lock_metric > 40) { // Требуем 40% выраженности любой из осей для TRACK
                loop->mode = COSTAS_MODE_TRACKING;
            }
        } else {
            if (loop->lock_metric < 20) { // Если упало ниже 20% - гарантированный срыв или тишина
                loop->mode = COSTAS_MODE_ACQUISITION;
            }
        }

        *is_locked = (loop->mode == COSTAS_MODE_TRACKING) ? 1 : 0;


        // 3. АВТОМАТ РЕЖИМОВ (FSM)
        uint8_t shift_p, shift_i;
        if (*is_locked) {
            loop->mode = COSTAS_MODE_TRACKING;
            shift_p = 6;  // Узкая петля: фильтруем шумы
            shift_i = 10;
        } else {
            loop->mode = COSTAS_MODE_ACQUISITION;
            shift_p = 3;  // Широкая петля: агрессивно выправляем срыв фазы
            shift_i = 7;
        }

        // 4. ДИСКРИМИНАТОРЫ: Фазовый (Костас) + Частотный (FLL)
        int32_t phase_error = (loop->sum_q * sign_i) >> 8;

        // Вычисляем Cross-Product FLL ошибку
        int64_t fll_prod = (int64_t)loop->prev_sum_i * loop->sum_q - (int64_t)loop->prev_sum_q * loop->sum_i;
        int64_t fll_dot  = (int64_t)loop->prev_sum_i * loop->sum_i + (int64_t)loop->prev_sum_q * loop->sum_q;
        int32_t fll_error = (int32_t)(fll_prod >> 16);
        if (fll_dot < 0) fll_error = -fll_error;

        // Комбинируем ошибку в зависимости от текущего режима работы
        int32_t total_error = 0;

        if (loop->mode == COSTAS_MODE_ACQUISITION) {
            // В режиме ЗАХВАТА жестко используем и фазу, и частоту для мгновенного зацепления
            total_error = phase_error + fll_error;
        } else {
            // В режиме СЛЕЖЕНИЯ полностью выключаем FLL (или делим его >> 4),
            // чтобы он не раскачивал петлю, и доверяем управление чистому Костасу
            total_error = phase_error;
        }

        // 5. Фильтрация и шаг NCO с адаптивными коэффициентами
        // Сделаем режим TRACK еще более демпфированным (узкополосным), чтобы убрать качели
        if (loop->mode == COSTAS_MODE_TRACKING) {
            shift_p = 7;   // Kp = 2^(-7)  - мягкое, плавное удержание фазы
            shift_i = 11;  // Ki = 2^(-11) - сверхмедленный интегратор для подавления дрейфа
        } else {
            shift_p = 3;   // Жесткий, агрессивный захват частоты
            shift_i = 7;
        }

        loop->integrator += (total_error >> shift_i);
        int32_t loop_output = (total_error >> shift_p) + loop->integrator;

        if (loop_output > 400)  loop_output = 400;
        if (loop_output < -400) loop_output = -400;

        loop->phase_inc = (uint16_t)(loop->base_phase_inc + loop_output);

        // Сохраняем память для FLL
        loop->prev_sum_i = loop->sum_i;
        loop->prev_sum_q = loop->sum_q;

        // Сброс накопителей
        loop->sum_i = 0; loop->sum_q = 0; loop->sum_energy = 0;
    }
}


