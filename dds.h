#ifndef DDS_H_
#define DDS_H_

#include "complex_math.h"

extern int16_t sine_table[1024];
typedef struct {
	uint32_t PhaseAccumulator;	// основа всего: старшие 8 бит это индекс в SinTab, а само значение это моментальная фаза
	uint32_t FreqFTW;			// Аккумулятор на каждом тике (опора 8кГц) инкрементируется на эту величину
	int8_t 	 PhaseAdjust;		// знаковая 8бит поправка фазы - СО. Если 0, то выходной сигнал строго синфазен "идеальному"
} DDS_Core_t;

void DDS_Init (DDS_Core_t *dds, uint16_t SampleFrequencyHz, uint16_t BaseToneHz);
void DDS_Tick (DDS_Core_t *dds, cplx_i16 *DAC_Out);
void DDS_RotatePhase (DDS_Core_t *dds, int16_t PhaseDegrees);
void DDS_SetPhaseAbsolute (DDS_Core_t *dds, int16_t PhaseDegrees);

/* дублирующие системы - переделать или удалить */
// Структура DDS синтезатора
typedef struct {
    uint32_t phase_accumulator;
    uint32_t phase_step;
} dds_t;

void init_dds_table(void);
// Функция генерации одного I/Q сэмпла (выход в float для удобства записи в WAV на ПК)
// В МК вы можете сразу приводить к int16_t
void dds_generate_sample(dds_t *dds, float *out_i, float *out_q);
// Установка частоты DDS
void dds_set_frequency(dds_t *dds, float frequency);
// Генерация тишины
void generate_silence(float duration, float *buf_i, float *buf_q, int *offset);
// Генерация тона (пилота или символа) заданной длительности
void generate_tone(dds_t *dds, float frequency, int num_samples, float *buf_i, float *buf_q, int *offset);

#include "trx.h"
void dds_set_tone(dds_t *dds, Tone_t tone);

#endif /* DDS_H_ */
