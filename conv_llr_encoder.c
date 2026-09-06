#include "conv_llr_encoder.h"
#include <string.h>
#include <stdio.h>

static unsigned char next_state[NUM_STATES][2];

const uint8_t out_bits_table[128] = {
    0x00, 0x03, 0x03, 0x00, 0x01, 0x02, 0x02, 0x01, 0x03, 0x00, 0x00, 0x03, 0x02, 0x01, 0x01, 0x02,
    0x03, 0x00, 0x00, 0x03, 0x02, 0x01, 0x01, 0x02, 0x00, 0x03, 0x03, 0x00, 0x01, 0x02, 0x02, 0x01,
    0x01, 0x02, 0x02, 0x01, 0x00, 0x03, 0x03, 0x00, 0x02, 0x01, 0x01, 0x02, 0x03, 0x00, 0x00, 0x03,
    0x02, 0x01, 0x01, 0x02, 0x03, 0x00, 0x00, 0x03, 0x01, 0x02, 0x02, 0x01, 0x00, 0x03, 0x03, 0x00,
    0x03, 0x00, 0x00, 0x03, 0x02, 0x01, 0x01, 0x02, 0x00, 0x03, 0x03, 0x00, 0x01, 0x02, 0x02, 0x01,
    0x00, 0x03, 0x03, 0x00, 0x01, 0x02, 0x02, 0x01, 0x03, 0x00, 0x00, 0x03, 0x02, 0x01, 0x01, 0x02,
    0x02, 0x01, 0x01, 0x02, 0x03, 0x00, 0x00, 0x03, 0x01, 0x02, 0x02, 0x01, 0x00, 0x03, 0x03, 0x00,
    0x01, 0x02, 0x02, 0x01, 0x00, 0x03, 0x03, 0x00, 0x02, 0x01, 0x01, 0x02, 0x03, 0x00, 0x00, 0x03
};

// Быстрый параллельный подсчет четности 7-битного числа для RISC-V без циклов
static inline uint8_t riscv_fast_parity7(uint32_t x) {
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1;
}

// Вспомогательная функция подсчета четности (Parity) без привязки к архитектуре
static inline uint8_t calc_parity(uint32_t val) {
    val ^= val >> 16;
    val ^= val >> 8;
    val ^= val >> 4;
    val ^= val >> 2;
    val ^= val >> 1;
    return val & 1;
}

static unsigned char prev_state[NUM_STATES][2];

void viterbi_table_gen (void) { /* предкомпиляция таблицы - сгенерированный массив надо добавить в этот же файл */
	FILE *fviterbi_table = fopen("viterbi_table.c", "wt");
    fprintf(fviterbi_table, "const unsigned char out_bits_table[128] = {");
    for (int state = 0; state < 64; state++) {
    	if ((state%8) == 0) fprintf(fviterbi_table, "\n\t");
        for (int bit = 0; bit <= 1; bit++) {
            // Собираем полное 7-битное окно кодера
            // В зависимости от реализации вашей функции conv_encode:
            // Новый бит задвигается либо слева, либо справа.
            // Стандартная схема: новый бит становится старшим (bit 6), старые сдвигаются вниз.
        	uint32_t state7 = ((state << 1) | bit) & 0x7F;

            // Считаем свертку (четность) для G1 и G2
            // Вычисляем биты свертки
            uint8_t g1_bit = calc_parity(state7 & NEW_POLY_G1);
            uint8_t g2_bit = calc_parity(state7 & NEW_POLY_G2);

            // Упаковываем: G1 идет в старший бит дибита, G2 - в младший
            uint8_t packed_dibit = (g1_bit << 1) | g2_bit;

            fprintf(fviterbi_table, "0x%02X, ", packed_dibit);
        }
    }
    fprintf(fviterbi_table, "\n};\n");
    fclose(fviterbi_table);
}

// Входной массив in_bits: 112 бит от КРС (каждый бит в своем байте 0 или 1)
// Выходной массив out_bits_1_2: 236 жестких кодированных бит (118 * 2)
void conv_encode_short_1_2(conv_llr_ack_encoder_t *enc, const uint8_t *in_bits, uint8_t *out_bits_1_2) {
    int i;
    uint32_t state7;
    enc->reg = 0; // Сброс регистра перед началом пакета

    // 1. Кодируем 112 информационных бит пакета КРС
    for (i = 0; i < 112; i++) {
        state7 = ((enc->reg << 1) | in_bits[i]) & 0x7F;
        out_bits_1_2[i * 2]     = riscv_fast_parity7(state7 & NEW_POLY_G1);
        out_bits_1_2[i * 2 + 1] = riscv_fast_parity7(state7 & NEW_POLY_G2);
        enc->reg = state7 & 0x3F;
    }

    // 2. ДОБАВЛЯЕМ ХВОСТ (Tail Bits) — 6 нулей для сброса решетки
    for (i = 112; i < 118; i++) {
        state7 = (enc->reg << 1) & 0x7F; // Задвигаем 0
        out_bits_1_2[i * 2]     = riscv_fast_parity7(state7 & NEW_POLY_G1);
        out_bits_1_2[i * 2 + 1] = riscv_fast_parity7(state7 & NEW_POLY_G2);
        enc->reg = state7 & 0x3F;
    }
}

void viterbi_llr_decode_soft_1_2_ack(const int8_t *in_soft_bits_236, uint8_t *out_decoded_bits) {
    // Внутренние метрики состояний (всего 2 * 64 * 4 байта = 512 байт на стеке)
    uint32_t metrics[64];
    uint32_t next_metrics[64];

    // Сжатая таблица истории для КОРОТКОГО пакета.
    // Занимает ВСЕГО 944 байта в ОЗУ! Ключевое слово static оставляет её в секции .bss
    static uint8_t path_history[SHORT_STEPS][8];

    // 1. Инициализация метрик: стартуем строго из нулевого состояния
    metrics[0] = 0;
    for (int i = 1; i < 64; i++) {
        metrics[i] = INF_METRIC;
    }

    // Очищаем историю путей перед началом декодирования пакета
    for (int s = 0; s < SHORT_STEPS; s++) {
        for (int b = 0; b < 8; b++) path_history[s][b] = 0;
    }

    // 2. ПРЯМОЙ ХОД (ACS — Add, Compare, Select)
    for (int step = 0; step < SHORT_STEPS; step++) {
        // Извлекаем пару знаковых LLR (-127...127)
        int32_t soft_r1 = in_soft_bits_236[step * 2];
        int32_t soft_r2 = in_soft_bits_236[step * 2 + 1];

        // Готовим буфер под новые метрики
        for (int i = 0; i < 64; i++) {
            next_metrics[i] = INF_METRIC;
        }

        // Перебор всех текущих состояний
        for (int i = 0; i < 64; i++) {
            if (metrics[i] >= INF_METRIC) continue; // Пропускаем неактивные траектории

            // Переход по информационному биту (0 или 1)
            for (int bit = 0; bit < 2; bit++) {
                // Извлекаем эталонный дибит из Flash-таблицы, которую мы сгенерировали
                // Помним, что в вашем коде индекс: (состояние << 1) | новый_бит
                // out_bits_table хранит: bit 1 = G1, bit 0 = G2
                uint8_t expected_outputs = out_bits_table[(i << 1) | bit];
                uint8_t exp_g1 = (expected_outputs >> 1) & 1;
                uint8_t exp_g2 = expected_outputs & 1;

                // Евклидово расстояние (метрика ветви) в LLR-представлении БЕЗ abs()!
                // Если кодер ожидал '0' (exp == 0), идеальный LLR должен быть +127.
                // Ошибка (расстояние) растет, если LLR уходит в минус.
                // Формула: dist = 127 - LLR (для ожидания 0) или 127 + LLR (для ожидания 1)
                uint32_t dist = 0;
                dist += exp_g1 ? (127 + soft_r1) : (127 - soft_r1);
                dist += exp_g2 ? (127 + soft_r2) : (127 - soft_r2);

                uint32_t new_metric = metrics[i] + dist;

                // Вычисляем индекс следующего состояния (новый бит задвигается СПРАВА)
                // Точно так же, как в вашем исходном коде:
                int next = ((i << 1) | bit) & 0x3F;

                // Выбор лучшего пути
                if (new_metric < next_metrics[next]) {
                    next_metrics[next] = new_metric;

                    // Ваша гениальная битовая упаковка предка
                    int byte_idx = next / 8;
                    int bit_idx  = next % 8;

                    // Так как новый бит задвигается справа, «уходящим» (предком)
                    // является старший (5-й) бит текущего состояния 'i'
                    if ((i >> 5) & 1) {
                        path_history[step][byte_idx] |= (1 << bit_idx);
                    } else {
                        path_history[step][byte_idx] &= ~(1 << bit_idx);
                    }
                }
            }
        }

        // Обновляем метрики для следующего шага
        // Добавляем защиту от переполнения: если минимальная метрика стала большой,
        // вычитаем её из всех выживших состояний (нормализация)
        uint32_t min_metric = next_metrics[0];
        for (int i = 1; i < 64; i++) {
            if (next_metrics[i] < min_metric) min_metric = next_metrics[i];
        }

        // Переносим метрики с нормализацией
        for (int i = 0; i < 64; i++) {
            if (next_metrics[i] < INF_METRIC) {
                metrics[i] = next_metrics[i] - min_metric;
            } else {
                metrics[i] = INF_METRIC;
            }
        }
    }

    // 3. ОБРАТНЫЙ ХОД (TRACEBACK) — O(1) за шаг
    // Очищаем ровно 14 байт (112 бит) выходного буфера для Рида-Соломона
	memset(out_decoded_bits, 0, 14);

	// Концепция Zero-Tail: кодер гарантированно закончил работу в состоянии 0 [1]
	int curr_state = 0;

	// Идем обратно по всей решетке от 117 до 0 шага [1]
	for (int step = (SHORT_STEPS - 1); step >= 0; step--) {
		// Восстанавливаемый информационный бит — это всегда младший бит текущего состояния
		uint8_t bit = (uint8_t)(curr_state & 1);

		// Записываем бит в выходной буфер только если мы вышли из зоны хвоста.
		// Шаги 117...112 — это хвостовые нули, их отбрасываем.
		// Шаги 111...0 — это полезные 112 бит данных пакета.
		if (step < 112) {
			// Упаковка бита в байт (MSB-first: от старшего бита к младшему)
			int byte_pos = step / 8;
			int bit_pos  = 7 - (step % 8);
			out_decoded_bits[byte_pos] |= (bit << bit_pos);
		}

		// МАТЕМАТИЧЕСКОЕ ВОССТАНОВЛЕНИЕ ПРЕДКА О(1):
		// Извлекаем сохраненный старший бит предка из компактной таблицы истории
		int byte_idx = curr_state / 8;
		int bit_idx  = curr_state % 8;
		uint8_t decision = (path_history[step][byte_idx] >> bit_idx) & 1;

		// Собираем предка: сдвигаем текущее состояние вправо
		// и возвращаем сохраненный бит 'decision' на место 5-го бита (MSB)
		curr_state = ((curr_state >> 1) | (decision << 5)) & 0x3F;
	}
}
