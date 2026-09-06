/*
 * dds.h
 *
 *  Created on: Sep 5, 2026
 *      Author: eugene
 */

#ifndef DDS_H_
#define DDS_H_

typedef struct {
	uint32_t PhaseAccumulator;	// основа всего: старшие 8 бит это индекс в SinTab, а само значение это моментальная фаза
	uint32_t FreqFTW;			// Аккумулятор на каждом тике (опора 8кГц) инкрементируется на эту величину
	int8_t 	 PhaseAdjust;		// знаковая 8бит поправка фазы - СО. Если 0, то выходной сигнал строго синфазен "идеальному"
} DDS_Core_t;

void DDS_Init (DDS_Core_t *dds, uint16_t SampleFrequencyHz, uint16_t BaseToneHz);
void DDS_Tick (DDS_Core_t *dds, cplx_i16 *DAC_Out);
void DDS_RotatePhase (DDS_Core_t *dds, int16_t PhaseDegrees);
void DDS_SetPhaseAbsolute (DDS_Core_t *dds, int16_t PhaseDegrees);


#endif /* DDS_H_ */
