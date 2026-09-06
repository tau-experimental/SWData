#include "clk_detect.h"

void clk_detect_init(clk_detect_t *clk) {
    clk->sample_counter = 0;
    clk->max_samples_per_sym = 256; // Номинал под 31.25 Бод

    clk->prev_center_i = 0; clk->prev_center_q = 0;
    clk->mid_joint_i   = 0; clk->mid_joint_q   = 0;
    clk->cur_center_i  = 0; clk->cur_center_q  = 0;

    // Настройки PI-петли тактовой синхронизации (коэффициенты-маховики)
#if 0
    clk->kp_shift = 7;  // Kp = 1/128
    clk->ki_shift = 11; // Ki = 1/2048 (медленный, тяжелый интегратор SCO дрейфа)
#else
    clk->kp_shift = 5;  // Kp = 1/64
    clk->ki_shift = 10; // Ki = 1/8192 (очень тяжелый маховик удержания тактовой частоты)
#endif
    clk->clk_integrator = 0;

    clk->symbol_ready = 0;
}

void clk_detect_process(clk_detect_t *clk, int16_t baseband_i, int16_t baseband_q,
                        int16_t *out_bit_i, int16_t *out_bit_q)
{
    clk->symbol_ready = 0; // Сбрасываем флаг готовности на каждом тике 8 кГц

    // --- 1. Фиксация строба "СТЫК СИМВОЛОВ" (Отсчет №0) ---
    if (clk->sample_counter == 0) {
        clk->mid_joint_i = baseband_i;
        clk->mid_joint_q = baseband_q;
    }

    // --- 2. Фиксация строба "ЦЕНТР СИМВОЛОВА" (Отсчет №128) ---
    // Номинальная середина символа. Здесь мы считываем "мягкое решение" для Витерби
    if (clk->sample_counter == 128) {
        // Сдвигаем историю центров символов
        clk->prev_center_i = clk->cur_center_i;
        clk->prev_center_q = clk->cur_center_q;

        // Запоминаем текущий чистый центр
        clk->cur_center_i = baseband_i;
        clk->cur_center_q = baseband_q;

        // Выдаем квадратурную точку наружу для дальнейшего конвейера (Коррелятор -> Витерби)
        *out_bit_i = clk->cur_center_i;
        *out_bit_q = clk->cur_center_q;

        // Машем флажком остальной системе: "Есть символьная точка 31.25 Бод!"
        clk->symbol_ready = 1;
    }

    // --- 3. Шаг счетчика и математика автоподстройки тайминга ---
    clk->sample_counter++;

    // Проверяем, добежал ли счетчик до конца текущей (возможно скорректированной) длины символа
    // --- ДИСКРИМИНАТОР ГАРДНЕРА (НЕКОГЕРЕНТНАЯ МОДЕРНИЗАЦИЯ) ---
    if (clk->sample_counter >= clk->max_samples_per_sym) {
        clk->sample_counter = 0;

        // Возвращаем каноническую когерентную формулу Гарднера
        int32_t diff_i = (int32_t)clk->cur_center_i - clk->prev_center_i;
        int32_t diff_q = (int32_t)clk->cur_center_q - clk->prev_center_q;

        int32_t err_i = ((int32_t)clk->mid_joint_i * diff_i) >> 15;
        int32_t err_q = ((int32_t)clk->mid_joint_q * diff_q) >> 15;
        int32_t clk_error = err_i + err_q;


        // [Остальной блок фильтрации и Anti-windup оставляем без изменений]
        if (clk_error > 32767)  clk_error = 32767;
        if (clk_error < -32768) clk_error = -32768;

        clk->clk_integrator += clk_error;

        clk->clk_integrator += clk_error;

        // Строгое ограничение физического джиттера до +-2 сэмплов
        int32_t max_clk_integral = 2 << clk->ki_shift;
        if (clk->clk_integrator > max_clk_integral)  clk->clk_integrator = max_clk_integral;
        if (clk->clk_integrator < -max_clk_integral) clk->clk_integrator = -max_clk_integral;

        int32_t total_adjust = clk->clk_integrator >> clk->ki_shift;

        /*int32_t max_clk_integral = 250 << clk->ki_shift;
        if (clk->clk_integrator > max_clk_integral)  clk->clk_integrator = max_clk_integral;
        if (clk->clk_integrator < -max_clk_integral) clk->clk_integrator = -max_clk_integral;

        int32_t total_adjust = clk->clk_integrator >> clk->ki_shift;*/

        #define CLK_THRESHOLD_SMOOTH 120
        if (total_adjust > CLK_THRESHOLD_SMOOTH)       clk->max_samples_per_sym = 255;
        else if (total_adjust < -CLK_THRESHOLD_SMOOTH) clk->max_samples_per_sym = 257;
        else                                           clk->max_samples_per_sym = 256;
    }

}
