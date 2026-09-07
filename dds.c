#include "dds.h"
#include "trx.h"


int16_t SinTab[256]; // ToDo: в боевой системе это прекомпилированная таблица во флэш, в модели - просто массив.
// Разделяемый ресурс: используется множеством функций!

void DDS_Init (DDS_Core_t *dds, uint16_t SampleFrequencyHz, uint16_t BaseToneHz) {
	// Fout = FTW * Fclk / 2^N, N=16
	//uint32_t Tuning = (65536UL * ((float)BaseToneHz/(float)SampleFrequencyHz));
	//dds->FreqFTW = (uint16_t)( Tuning ) ;
	dds->FreqFTW = (uint32_t)(((uint64_t)BaseToneHz << 32) / (float)SampleFrequencyHz);
	dds->PhaseAccumulator = 0;
	dds->PhaseAdjust = 0;
	for (int i = 0; i<256; i++) {
		float x = (float)i*2*M_PI/256.0;
		SinTab[i] = (int16_t)(32767.0 * sin(x));
	}
}

void DDS_Tick (DDS_Core_t *dds, cplx_i16 *DAC_Out) {
    // Извлекаем базовый индекс (0..255)
    uint8_t BaseIndex = (uint8_t)(dds->PhaseAccumulator >> 24);

    // Складываем все как uint8_t. Сложение беззнакового и знакового (PhaseAdjust)
    // в Си автоматически приведет PhaseAdjust к uint8_t. Сдвиг по модулю 256 произойдет сам.
    uint8_t Index_Sin = (uint8_t)(BaseIndex + dds->PhaseAdjust);
    uint8_t Index_Cos = (uint8_t)(Index_Sin + 64);

    // Выдача в ЦАП (Re = cos, Im = sin для корректного комплексного сигнала)
    DAC_Out->re = SinTab[Index_Cos];
    DAC_Out->im = SinTab[Index_Sin];

    dds->PhaseAccumulator += dds->FreqFTW;
}

void DDS_RotatePhase (DDS_Core_t *dds, int16_t PhaseDegrees) {
	/* понимаем только 0, +-45, +-90, +-135, 180
	 * 90 градусов это 64 единицы */
    /* Накопление фазы: два вызова по +45 дадут +90.
     * Используем приведение к uint8_t, так как переполнение
     * беззнаковых типов строго определено стандартом Си (модуль 256) */
    uint8_t current_adjust = (uint8_t)dds->PhaseAdjust;

    switch (PhaseDegrees) {
        case -135: current_adjust -= 96;  break;
        case -90:  current_adjust -= 64;  break;
        case -45:  current_adjust -= 32;  break;
        case 0:    return; // экономим время, фаза не меняется
        case 45:   current_adjust += 32;  break;
        case 90:   current_adjust += 64;  break;
        case 135:  current_adjust += 96;  break;
        case -180:
        case 180:  current_adjust += 128; break; // Здесь 128 аппаратно эквивалентно -128
        default:   return;
    }

    // Возвращаем значение обратно в знаковую переменную.
    // Битовая маска сохраняется, компилятор доволен.
    dds->PhaseAdjust = (int8_t)current_adjust;
}

void DDS_SetPhaseAbsolute (DDS_Core_t *dds, int16_t PhaseDegrees) {
	/* понимаем только 0, +-45, +-90, +-135, 180
	 * 90 градусов это 64 единицы */
    switch (PhaseDegrees) {
        case -135: dds->PhaseAdjust = -96;  break;
        case -90:  dds->PhaseAdjust = -64;  break;
        case -45:  dds->PhaseAdjust = -32;  break;
        case 0:    dds->PhaseAdjust = 0; 	return;
        case 45:   dds->PhaseAdjust = 32;  break;
        case 90:   dds->PhaseAdjust = 64;  break;
        case 135:  dds->PhaseAdjust = 96;  break;
        case 180:  dds->PhaseAdjust = 128; break; // Здесь 128 аппаратно эквивалентно -128
        default:   return;
    }
}

// Таблица синуса на 256 точек (Q15 формат: 0..32767)
// В реальном МК лежит в Flash. Для теста генерируем один раз.
int16_t sine_table[1024];
void init_dds_table(void) {
    for (int i = 0; i < 1024; i++) {
        sine_table[i] = (int16_t)(sin(2.0 * M_PI * i / 1024.0) * 32767.0);
    }
}

void dds_generate_sample(dds_t *dds, float *out_i, float *out_q) {
    // Старшие 8 бит аккумулятора фазы — это индекс в таблице синуса (0..255)
    uint16_t index_i = (uint16_t)((dds->phase_accumulator >> 22) & 1023);
    // Для косинуса (Q) сдвигаем фазу на 90 градусов (64 отсчета для таблицы из 256 элементов)
    uint16_t index_q = (uint16_t)((index_i + 256) & 1023);

    *out_i = (float)sine_table[index_i] / 32767.0f;
    *out_q = (float)sine_table[index_q] / 32767.0f;

    // Шаг фазы вперед
    dds->phase_accumulator += dds->phase_step;
}

// Установка частоты DDS
inline void dds_set_frequency(dds_t *dds, float frequency) {
    // phase_step = (F_out * 2^32) / F_s
    dds->phase_step = (uint32_t)((frequency * 4294967296.0) / FS);
}

inline void dds_set_tone(dds_t *dds, Tone_t tone) {
	switch (tone) {
		case TONE_A: dds_set_frequency(dds, TONE(0)); return;
		case TONE_B: dds_set_frequency(dds, TONE(1)); return;
		case TONE_C: dds_set_frequency(dds, TONE(2)); return;
		case TONE_D: dds_set_frequency(dds, TONE(3)); return;
		default: return;
	}
}

// Генерация тишины
void generate_silence(float duration, float *buf_i, float *buf_q, int *offset) {
    int samples = (int)(duration * FS);
    for (int i = 0; i < samples; i++) {
        buf_i[*offset] = 0.0f;
        buf_q[*offset] = 0.0f;
        (*offset)++;
    }
}

// Генерация тона (пилота или символа) заданной длительности
void generate_tone(dds_t *dds, float frequency, int num_samples, float *buf_i, float *buf_q, int *offset) {
    dds_set_frequency(dds, frequency);
    for (int i = 0; i < num_samples; i++) {
        dds_generate_sample(dds, &buf_i[*offset], &buf_q[*offset]);
        (*offset)++;
    }
}

