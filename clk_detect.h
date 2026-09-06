#ifndef CLK_DETECT_H
#define CLK_DETECT_H

#include <stdint.h>

typedef struct {
    // Внутреннее время и кольцевой буфер
    uint16_t sample_counter;       // Текущий отсчет внутри символа (0 .. max_samples-1)
    uint16_t max_samples_per_sym;  // Динамическая длительность текущего символа (255, 256 или 257)

    // Хранилище отсчетов для дискриминатора Гарднера
    // Нам нужны 3 комплексные точки: Предыдущий Центр, Текущий Центр, и Середина (Стык) между ними
    int16_t prev_center_i, prev_center_q;
    int16_t mid_joint_i,   mid_joint_q;
    int16_t cur_center_i,  cur_center_q;

    // Коэффициенты петли тактовой автоподстройки (Fixed-Point битовые сдвиги)
    uint8_t kp_shift;              // Пропорциональный сдвиг
    uint8_t ki_shift;              // Интегральный сдвиг

    // Тяжелый интегратор расхождения частот тактовых генераторов (SCO)
    int32_t clk_integrator;        // Накапливает микро-дребезг ЦАП/АЦП

    // Выходной флаг синхронизации
    uint8_t symbol_ready;          // Выставляется в 1 строго в момент, когда символ полностью принят
} clk_detect_t;

/**
 * @brief Инициализация детектора тактовой частоты Гарднера
 */
void clk_detect_init(clk_detect_t *clk);

/**
 * @brief Поотсчетный конвейер тактовой синхронизации (8000 Гц)
 * @details Принимает отсчеты из петли Костаса. Внутри себя ведет учет времени.
 *          Когда сэмпл приходится на "Центр символа", функция сохраняет его
 *          и выставляет флаг symbol_ready.
 * @param baseband_i, baseband_q Сигнал с выхода петли Костаса (0 Гц)
 * @param out_bit_i, out_bit_q Точки центра символа (выдаются наружу, когда symbol_ready == 1)
 */
void clk_detect_process(clk_detect_t *clk, int16_t baseband_i, int16_t baseband_q,
                        int16_t *out_bit_i, int16_t *out_bit_q);

#endif // CLK_DETECT_H
