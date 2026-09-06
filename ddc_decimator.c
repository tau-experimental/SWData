#include "ddc_decimator.h"

// Коэффициенты КИХ-фильтра ФНЧ (полоса среза 125 Гц на частоте 8 кГц) в Fixed-Point Q15
// Рассчитаны так, чтобы сумма модулей не приводила к переполнению int32_t
static const int16_t fir_coeffs[FIR_TAPS] = {
	115, 137, 192, 281, 405, 560, 743, 945, 1159, 1373,
	1578, 1763, 1919, 2036, 2110, 2136, 2110, 2036, 1919, 1763,
	1578, 1373, 1159, 945, 743, 560, 405, 281, 192, 137, 115
};

// Кольцевой буфер истории сдвинутого по частоте сигнала (размер равен числу тапов)
static cplx_i16 fir_history[FIR_TAPS];
static int fir_write_ptr = 0;
static int decimation_counter = 0;

// Вход: ADC_In — сырой комплексный отсчет 8 кГц из WAV-файла
// Выход: LowFreq_Out — отсчет 500 Гц (заполняется только когда функция возвращает 1)
// Возвращает: 1 — если сформирован новый децимированный отсчет, 0 — в остальных случаях
int process_sample_downconvert_and_decimate(cplx_i16 ADC_In, DDS_Core_t *dds, cplx_i16 *LowFreq_Out) {
    cplx_i16 dds_val;
    // 1. Продвигаем DDS на один тик (несущая 1 кГц)
    DDS_Tick(dds, &dds_val);

    // 2. Перенос частоты вниз на 1 кГц: Z = ADC_In * conj(DDS)
    // Используем вашу функцию. На выходе получаем int32_t компоненты.
    cplx_i32 mixed32 = cplx_i32_mul_conj(ADC_In, dds_val);

    // Масштабируем обратно в int16_t со сдвигом (подбирается под разрядность АЦП)
    cplx_i16 mixed16;
    mixed16.re = (int16_t)(mixed32.re >> 15);
    mixed16.im = (int16_t)(mixed32.im >> 15);

    // 3. Записываем сдвинутый отсчет в историю КИХ-фильтра
    fir_history[fir_write_ptr] = mixed16;
    fir_write_ptr = (fir_write_ptr + 1) % FIR_TAPS;

    // 4. Считаем такт децимации
    decimation_counter++;
    if (decimation_counter >= DECIMATION_FACTOR) {
        decimation_counter = 0; // Сброс счетчика

        // Выполняем свертку КИХ-фильтра (только для этого 16-го отсчета!)
        int32_t acc_re = 0;
        int32_t acc_im = 0;
        int read_ptr = fir_write_ptr;

        for (int i = 0; i < FIR_TAPS; i++) {
            acc_re += (int32_t)fir_history[read_ptr].re * fir_coeffs[i];
            acc_im += (int32_t)fir_history[read_ptr].im * fir_coeffs[i];

            read_ptr = (read_ptr + 1) % FIR_TAPS;
        }

        // Финальный сдвиг из формата Q15 фильтра обратно в чистый int16_t
        LowFreq_Out->re = (int16_t)(acc_re >> 15);
        LowFreq_Out->im = (int16_t)(acc_im >> 15);

        return 1; // Новый сэмпл 500 Гц готов для отправки в БПФ-256
    }

    return 0; // Сэмпл пропущен (децимация)
}
