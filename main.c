#include <stdio.h>
#include <stdint.h>
#include "wav_io.h"

/* ВВОДНЫЕ
 * 1) частота сэмплирования АЦП\ЦАП: 8000 Гц
 * 2) Символьная скорость: 31.25 бод
 * 3) Модуляция: на этапе синхронизации DBPSK (двоичная), на этапе передачи данных pi/4-DQPSK (четверичная)
 * 4а) DBPSK означает, что
 * 	- если сейчас передаётся бит 0, то фаза не меняется относительно предыдущего тона (сдвиг фаз = 0);
 * 	- если сейчас передаётся бит 1, то фаза переворачивается относительно предыдущего на 180 град;
 * 	- тем самым,
 * 		передача битовой последовательности "0,0,0..." превращается в непрерывный тон без изменения фазы
 * 		передача "1,1,1,.." означает переключение фазы на 180 на каждом символе
 * 	- стабильность тактовой частоты обязана быть очень высокой (TCXO/VCXO)
 * 4б) DQPSK кодируется грей-кодами:
 * 	если передаётся дибит 00, то сдвиг +45
 * 	если передаётся дибит 01, то сдвиг +135
 * 	если передаётся дибит 11, то сдвиг -135
 * 	если передаётся дибит 10, то сдвиг -45
 * 5) Структура посылки DATA:
 * 	[Пилот-тон, 1сек] - для предварительного обнаружения и захвата частоты
 * 	[DBPSK 1010] - взведение системы поиска преамбулы
 * 	[DBPSK MLS-63] - поиск синхропоследовательности
 * 	[Пилот-тон, 1 символ] - перезахват частоты
 * 	[Payload, большой блок pi/4-DQPSK символов, перемещающихся пилот-тоном по схеме: 13 символов / 1 пилот-символ ]
 * 5) Структура посылки ACK:
 * 	[Пилот-тон, 1сек] - для предварительного обнаружения и захвата частоты
 * 	[DBPSK 1010] - взведение системы поиска преамбулы
 * 	[DBPSK MLS-63] - поиск синхропоследовательности
 * 	[Пилот-тон, 1 символ] - перезахват частоты
 * 	[Payload, pi/4-DQPSK символов, 56 символов всего, на каждые 14 символов передаётся 1 пилот-символ ]
 * 	1. 8 байт сырой нагрузки (то, что уходит в User Level) + 6 байта (RS(14, 8)) = 14 байт пакет после R-S
 * 	2. 28 байт данных -> 112 бит после Р-С + 6 нулевых "tail bits": 118 бит;
 * 	3. после свёрточного кодера Витерби (R=1/2) 236 бит
 * 	4. выравнивание до кратности 14: + 16 нолей -> 252 бита
 * 	5. Битовый премежитель, матрица M * N = 28 строк * 9 столбца:
 * 		- Каждая строка матрицы — это один полубайт RS(14,8) пакета (4 бит):
 * 			Строка 1: Первые 4 бита Байта 1
 * 			Строка 2: Вторые 4 бита Байта 1
 * 			...
 * 			Строка 28: Вторые 4 бита байта 14
 * 		- Вычитка в эфир (приём обратно): по столбцам, сначала передаются первые биты всех 14 байт,
 * 		затем вторые биты всех 14 байт и так далее; из считанного потока формируются дибиты для DQPSK:
 * 			первый символ формируется из [Столбец 1, Бит 1] и [Столбец 1, Бит 2];
 * 			второй символ — из [Столбец 1, Бит 3] и [Столбец 1, Бит 4].
 * 		Один столбец = 14 символов DQPSK, после него вставляется пилот-символ
 * 	6. Итого, весь Payload ACK это 9 столбцов по 14 бит + 9 пилот-символов = 135 символов, 4.32 секунды.
 * 	Декодирование должно использовать Log-Likelihood Ratio (LLR, "мягкие решения"):
 * 		решение по биту кодируется как int8_t;
 * 		-127 уверенность в "1", 0 неопределённость, +127 уверенность в "0"
 */



#if 0
typedef struct { /* Q15 или целочисленный отсчет АЦП */
    int16_t re, im;
} cplx_i16;
#else
#include "complex_math.h"
#endif



/* =============================================================================================
 * 	Целочисленное получение LLR (Fixed-Point) без float
 * ============================================================================================= */

#define RAW_LLR_SCALING		10 /*  Сдвиг (например, на 9-12 бит) подбирается под амплитуду вашего сигнала на АЦП,
       чтобы при хорошем сигнале значения упирались в насыщение, но не шумели.  */

/* Пусть отсчеты с АЦП/согласованного фильтра приходят как пары cplx_i16 (re=I, im=Q).
 * Для текущего символа Y_k = (I_k, Q_k) и предыдущего Y_{k-1} = (I_{k-1}, Q_{k-1}): */
void dqpsk_demod_to_llr(cplx_i16 Curr, cplx_i16 Prev, int8_t *llr_b1, int8_t *llr_b2) {
/* Вычисление дифференциальной фазы и мгновенного LLR */
    /* Комплексное сопряженное умножение: Z = Y_curr * conj(Y_prev)
       Z_I = I_curr * I_prev + Q_curr * Q_prev
       Z_Q = Q_curr * I_prev - I_curr * Q_prev */
    cplx_i32 Z = cplx_i32_mul_conj(Curr, Prev);

    /* Нормализация под диапазон int8_t (-127...127) */
    int32_t raw_llr1 = Z.re >> RAW_LLR_SCALING;
    int32_t raw_llr2 = Z.im >> RAW_LLR_SCALING;

    // Жесткое насыщение (Saturate) для предотвращения переполнения целых чисел
    if (raw_llr1 > 127)  raw_llr1 = 127;
    if (raw_llr1 < -127) raw_llr1 = -127;
    if (raw_llr2 > 127)  raw_llr2 = 127;
    if (raw_llr2 < -127) raw_llr2 = -127;

    *llr_b1 = (int8_t)raw_llr1;
    *llr_b2 = (int8_t)raw_llr2;
}

/* =============================================================================================
 * 	Деперемежитель 28*9 (252 байта RAM под таблицу)
 * ============================================================================================= */
#define INTERLEAVER_ROWS 28
#define INTERLEAVER_COLS 9
#define INTERLEAVER_SIZE (INTERLEAVER_ROWS * INTERLEAVER_COLS) // 252

/* Матрица 28x9 размещается в памяти как одномерный массив из 252 байт.
 * Запись идет по колонкам (из демодулятора), чтение — по строкам (в Витерби). */

static int8_t deinterleaver_matrix[INTERLEAVER_SIZE]; // Буфер деперемежителя в ОЗУ

// 1. Функция последовательной записи колонок из эфира
// Вызывать 9 раз (для каждой группы из 14 информационных символов между пилотами)
void deinterleaver_write_column(uint8_t col_idx, const int8_t *incoming_llr_28) {
    if (col_idx >= INTERLEAVER_COLS) return;

    for (int r = 0; r < INTERLEAVER_ROWS; r++) {
        // Индексация для хранения: Row-major, но пишем поколоночно
        deinterleaver_matrix[r * INTERLEAVER_COLS + col_idx] = incoming_llr_28[r];
    }
}

// 2. Функция чтения плоского потока для декодера Витерби
// Вычитывает матрицу строго построчно в линейный буфер
void deinterleaver_read_flat(int8_t *output_llr_236) {
    int out_idx = 0;
    for (int r = 0; r < INTERLEAVER_ROWS; r++) {
        for (int c = 0; c < INTERLEAVER_COLS; c++) {
            // Отрезаем последние 16 бит padding (они в самом конце матрицы)
            if (out_idx < 236) {
                output_llr_236[out_idx++] = deinterleaver_matrix[r * INTERLEAVER_COLS + c];
            }
        }
    }
}

/* =============================================================================================
 * 	Интеграция с логикой пилотов-символов (Зануление LLR)
 * ============================================================================================= */
/* Пилот-символы позволяют легко реализовать «мягкое стирание».
 * Если после обработки пилота мы видим, что фаза «скакнула» или амплитуда упала ниже порога,
 * мы можем занулить метрики для всей колонки, которая шла перед этим пилотом: */
void force_column_erasure(uint8_t col_idx) {
    for (int r = 0; r < INTERLEAVER_ROWS; r++) {
        deinterleaver_matrix[r * INTERLEAVER_COLS + col_idx] = 0; // 0 означает полную неопределенность
    }
}

/* =============================================================================================
 * 	Метрики для декодера Витерби (Fixed-Point)
 * ============================================================================================= */
/* Когда мы передаём массив int8_t output_llr_236 в декодер Витерби,
 * расчет метрик ветвей (Branch Metrics) на слабом ядре без FPU превращается в
 * простейшее сложение.
 * Для каждого шага и каждого гипотетического бита (0 или 1): */
// Псевдокод расчета метрики внутри бабочки Витерби
///// int8_t received_llr = output_llr_236[bit_idx];

// Если кодер предполагает, что был передан бит '0' (ожидаем +LLR):
// Метрика ошибки тем больше, чем ближе LLR к -127
///// int32_t branch_metric_0 = 127 - (int32_t)received_llr;

// Если кодер предполагает, что был передан бит '1' (ожидаем -LLR):
///// int32_t branch_metric_1 = 127 + (int32_t)received_llr;

/* Если received_llr == 0 (канал стерт помехой), обе метрики branch_metric_0 и branch_metric_1
 * станут равны 127. Декодер Витерби не получит никакого предпочтения ни для 0, ни для 1,
 * и корректно пройдет этот шаг исключительно за счет накопленной истории предыдущих надежных состояний. */

#include "conv_llr_encoder.h"

int viterbi_ack_test(void) {
    printf("=== СТАРТ ВЕРИФИКАЦИИ ТРАКТА ВИТЕРБИ И ПЕРЕМЕЖИТЕЛЯ ===\n");

    // 1. Исходный тестовый пакет от Рида-Соломона (14 байт (8 байт данные, 6 байт проверочные)
    // Возьмем фиксированный паттерн для наглядности
    uint8_t tx_rs_packet[14] = {
        0xAA, 0x55, 0x12, 0x34, 0x56, 0x78, 0x9A,
        0xBC, 0xDE, 0xF0, 0x11, 0x22, 0x33, 0x44
    };

    // Превращаем 14 байт в плоский массив из 112 отдельных бит (0 или 1)
    // Это нужно, так как наш кодер conv_encode_short_1_2 принимает биты побайтово
    uint8_t tx_bits[112];
    for (int i = 0; i < 112; i++) {
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8); // MSB first
        tx_bits[i] = (tx_rs_packet[byte_idx] >> bit_idx) & 1;
    }

    // 2. КОДИРОВАНИЕ СВЕРТОЧНЫМ КОДОМ (Передатчик)
    conv_llr_ack_encoder_t encoder;
    uint8_t tx_encoded_pure[236]; // 118 шагов * 2 бита

    conv_encode_short_1_2(&encoder, tx_bits, tx_encoded_pure);
    printf("[OK] Сверточное кодирование завершено. Получено %d бит.\n", 236);

    // 3. ПАДДИНГ И ЗАПИСЬ В ПЕРЕМЕЖИТЕЛЬ
    // Нам нужно дополнить 236 бит до 252 (добавить 16 нулей заполнения)
    uint8_t tx_interleaver_input[252];
    memcpy(tx_interleaver_input, tx_encoded_pure, 236);
    memset(tx_interleaver_input + 236, 0, 16); // Дописываем 16 нулей в конец

    // Запись в матрицу перемежителя 28 строк на 9 столбцов
    // (Построчная запись, поколоночное чтение)
    uint8_t tx_interleaver_matrix[28][9];
    int bit_ptr = 0;
    for (int r = 0; r < 28; r++) {
        for (int c = 0; c < 9; c++) {
            tx_interleaver_matrix[r][c] = tx_interleaver_input[bit_ptr++];
        }
    }

    // 4. СИМУЛЯЦИЯ ИДЕАЛЬНОГО КАНАЛА И ДЕПЕРЕМЕЖИТЕЛЯ (Приемник)
    // В эфир данные уходят по столбцам. Приемник принимает их по столбцам,
    // переводит жесткие биты (0/1) в жесткие LLR (+127/-127) и складывает в матрицу деперемежителя.
    int8_t rx_deinterleaver_matrix[28][9];
    // Выберем столбец, который будет ПОЛНОСТЬЮ уничтожен замиранием (например, 4-й столбец)
    int erased_column_idx = 3;

    for (int c = 0; c < 9; c++) {
        int8_t rx_column_llr[28];
        for (int r = 0; r < 28; r++) {
            // Извлекаем бит из эфирного столбца
            uint8_t channel_bit = tx_interleaver_matrix[r][c];
            // Переводим в формат LLR: 0 -> +127, 1 -> -127
            //rx_column_llr[r] = (channel_bit == 0) ? 127 : -127;
            if (c == erased_column_idx) {
                // ИМИТАЦИЯ КРАХА КАНАЛА: сигнал ушел в ноль.
                // Демодулятор выдает 0 (полная неопределенность/стирание)
                rx_column_llr[r] = 0;
            } else {
                // Штатный чистый прием
                rx_column_llr[r] = (channel_bit == 0) ? 127 : -127;
            }
        }

        // Передаем колонку в функцию деперемежителя (которую мы написали ранее)
        // В нашем коде матрица лежит как одномерный массив deinterleaver_matrix[252]
        // Имитируем вызов функции deinterleaver_write_column:
        deinterleaver_write_column(c, rx_column_llr);
    }
    if (erased_column_idx >= 0) {
        printf("[WARN] Имитация замирания: Столбец %d полностью занулен (стирание).\n", erased_column_idx);
    }
    printf("[OK] Эфир пройден. LLR сформированы и деперемежены.\n");

    // 5. ВЫЧИТЫВАНИЕ ПЛОСКОГО ПОТОКА ИЗ ДЕПЕРЕМЕЖИТЕЛЯ
    int8_t rx_llr_to_viterbi[236];
    // Вызываем функцию чтения, которая должна сама отрезать 16 бит паддинга
    deinterleaver_read_flat(rx_llr_to_viterbi);

    // 6. ДЕКОДИРОВАНИЕ ВИТЕРБИ С ИСПОЛЬЗОВАНИЕМ LLR
    uint8_t rx_decoded_bytes[14];
    viterbi_llr_decode_soft_1_2_ack (rx_llr_to_viterbi, rx_decoded_bytes);
    printf("[OK] Декодирование Витерби завершено.\n");

    // 7. СРАВНЕНИЕ РЕЗУЛЬТАТОВ
    printf("\n=== СЛИЧЕНИЕ ДАННЫХ ===\n");
    printf("Передано: ");
    for(int i=0; i<14; i++) printf("%02X ", tx_rs_packet[i]);
    printf("\nПринято:  ");
    for(int i=0; i<14; i++) printf("%02X ", rx_decoded_bytes[i]);
    printf("\n");

    if (memcmp(tx_rs_packet, rx_decoded_bytes, 14) == 0) {
        printf("\n>>>> УСПЕХ! Данные полностью совпадают бинарно! <<<<\n");
    } else {
        printf("\n>>>> ОШИБКА! Данные искажены! <<<<\n");
    }

    return 0;
}

#include "galois_field.h"

#define RS_MAX_2T  32
void generate_correct_poly(int n, int k) {
	int t2 = n - k;
    uint8_t g[RS_MAX_2T + 1] = {0};
    g[0] = 1; // Стартуем с g(x) = 1

    for (int i = 1; i <= t2; i++) {
        // Находим корень root = alpha^i. Предполагаем alpha = 2.
        uint8_t root = 1;
        for (int r = 0; r < i; r++) {
            root = gf_mul(root, 2);
        }

        uint8_t next_g[RS_MAX_2T + 1] = {0};
        for (int j = 0; j < i; j++) {
            // Умножение текущего полинома на (x + root)
            next_g[j + 1] ^= g[j];               // Коэффициент при x^(j+1)
            next_g[j]     ^= gf_mul(g[j], root); // Коэффициент при x^j
        }

        // Копируем обратно в g
        for (int j = 0; j <= i; j++) {
            g[j] = next_g[j];
        }
    }

    // Выводим результат в консоль
    printf("const uint8_t gen_poly_size_%d[] = { ", t2);
    for (int i = 0; i <= t2; i++) {
        printf("0x%02X, ", g[i]);
    }
    printf(" };\n");
}

void rs_init_geometry_x(int n, int k) {
    int i, j;
    uint8_t g[RS_MAX_2T + 1] = {0};
    g[0] = 1; // Стартуем с g(x) = 1
    int t2 = n - k;

    /* Классическое построение: g(x) = (x + g^1)*(x + g^2)*...*(x + g^t2) */
    for (i = 1; i <= t2; i++) {
        unsigned char root = gf_exp[i]; // Ваши корни поля

        // Сдвиг и умножение делаются СТРОГО с конца полинома к началу!
        // j бежит от текущей максимальной степени i вниз до 0.
        for (j = i; j >= 0; j--) {
            unsigned char term_x = (j > 0) ? g[j - 1] : 0; // вклад от умножения на 'x'
            unsigned char term_r = gf_mul(g[j], root);    // вклад от умножения на 'root'

            g[j] = gf_add(term_x, term_r);
        }
    }
    for (int i = 0; i <= t2; i++) {
        printf("0x%02X, ", g[i]);
    }

}

#include "reed_solomon.h"

#define PAYLOAD_DATA_LENGTH		20
#define PAYLOAD_ACK_LENGTH		8
#define PAYLOAD_RS_LENGTH		6
#define DATA_TOTAL_LENGTH	(PAYLOAD_DATA_LENGTH + PAYLOAD_RS_LENGTH)
#define ACK_TOTAL_LENGTH	(PAYLOAD_ACK_LENGTH + PAYLOAD_RS_LENGTH)

#pragma pack(push, 1)
typedef union {
    // 1. Представление в виде единого сплошного массива
    uint8_t ENCODED[DATA_TOTAL_LENGTH];

    // 2. Представление в виде двух раздельных массивов
    struct {
        uint8_t DATA[PAYLOAD_DATA_LENGTH];
        uint8_t RS[PAYLOAD_RS_LENGTH];
    }; // Анонимная структура позволяет обращаться к полям напрямую
} RS_Payload_Data_t;

typedef union {
    // 1. Представление в виде единого сплошного массива
    uint8_t ENCODED[ACK_TOTAL_LENGTH];

    // 2. Представление в виде двух раздельных массивов
    struct {
        uint8_t DATA[PAYLOAD_ACK_LENGTH];
        uint8_t RS[PAYLOAD_RS_LENGTH];
    }; // Анонимная структура позволяет обращаться к полям напрямую
} RS_Payload_Ack_t;
#pragma pack(pop)

#include "dds.h"
#include "ddc_decimator.h"
#include "channel_sim.h"

void DDS_Test(void) {
	DDS_Core_t DDS_TestSubject;
	DDS_Core_t DDS_LO;
	FILE *dump = fopen("DDS_Dump.csv", "wt");
	DDS_Init (&DDS_TestSubject, 8000, 1010);
	DDS_Init (&DDS_LO, 8000, 1000);
	for (int i = 0; i < 8000; i++) { // 1 second
		cplx_i16 LO_Out, IF_Out;
		DDS_Tick (&DDS_TestSubject, &LO_Out);

		if (i == 3999) { DDS_RotatePhase (&DDS_TestSubject, +180); }

		if (process_sample_downconvert_and_decimate (LO_Out, &DDS_LO, &IF_Out)) {
			fprintf (dump, "%d, %d, %d\n", i, IF_Out.re, IF_Out.im);
		}
	}
	fclose(dump);
}

/* 31.25 baud @ 8000 SamplRate => 256 samples per symbol
 * Decimation:16 => 16 samples per symbol.
 * Для дифференциального перемножителя на низкой ПЧ нужен буфер на 32 выборки (16 от предыдущего символа и 16 от текущего)
 * */
#define RINGBUFSIZE	32

// Канонический массив знаков MLS-31
// Жесткий системный шаблон знаков М-последовательности MLS-31
#define MLS31_LEN	31
const int8_t mls_31_canonic_signs[MLS31_LEN] = {
		1,  1,  1,  1,  1, -1, -1,  1,  1, -1,
		1, -1,  1,  1,  1, -1,  1, -1, -1, -1,
		1, -1, -1,  1, -1,  1, -1,  1, -1, -1, -1
};

int8_t mls_31_diff_template_tx[(MLS31_LEN-1)]; /* 30 дифф.знаков для передатчика */
#define CORR_TEMPLATE_LENGTH	((MLS31_LEN-1)*16)
int8_t mls_31_diff_template_corr[CORR_TEMPLATE_LENGTH]; /* 30 дифф.знаков, 16 сэмплов каждый. Шаблон максимально тупой. */
int32_t diff_corr_buff_re[CORR_TEMPLATE_LENGTH]; /* кольцевой буфер для принимаемых "мягких оценок", максимально тупой */
int32_t diff_corr_buff_im[CORR_TEMPLATE_LENGTH];
void PrepMLS31DiffTemplate(void) {
	printf ("DiffMLS31: ");
	for (int i = 0; i < (MLS31_LEN-1); i++) {
		int8_t diff_sign = (mls_31_canonic_signs[i] == mls_31_canonic_signs[i+1]) ? 1 : -1;
		mls_31_diff_template_tx[i] = diff_sign;
		for (int j=0; j<16; j++) mls_31_diff_template_corr[i*16+j] = diff_sign;
		printf ("%d ", diff_sign);
	}
	printf ("\n");
}

int8_t mls_31_tx_symbols[MLS31_LEN]; // Символы, которые полетят в эфир
void PrepMLSTx(void) {
    // Стартовый опорный символ ( phase reference )
    mls_31_tx_symbols[0] = 1;

    for (int i = 1; i < MLS31_LEN; i++) {
        // s[n] = s[n-1] * b[n]
        // Используем mls_31_canonic_signs[i] как информационный бит
        mls_31_tx_symbols[i] = mls_31_tx_symbols[i-1] * mls_31_canonic_signs[i];
    }
}

void PrepMLSRxTemplate(void) {
    printf("Correct RX Template: ");

    // Идем по каноническим знакам от 1 до 30 (всего 30 символов)
    for (int i = 1; i < MLS31_LEN; i++) {
        int8_t real_sign = mls_31_canonic_signs[i];

        // Заполняем прямоугольный блок из 16 сэмплов для текущего символа
        for (int j = 0; j < 16; j++) {
            mls_31_diff_template_corr[(i - 1) * 16 + j] = real_sign;
        }
        printf("%d ", real_sign);
    }
    printf("\n");
}

/*
 * Устойчивость к сдвигу частоты (Дрейф фазы):
 *  Сейчас у вас идеальный канал, поэтому вещественная часть Dk.re дает максимальный пик.
 *  В реальном радиоэфире из-за неидеальности опорных генераторов созвездие DBPSK начнет
 *  медленно вращаться. Вещественная часть Dk.re из-за этого начнет уменьшаться,
 *  а мнимая Dk.im — расти.Решение: Чтобы коррелятор не терял пик при сдвиге частоты,
 *  считайте корреляцию отдельно для вещественной части и отдельно для мнимой
 *  (используя тот же вещественный шаблон). На выходе вы получите два значения:
 *  CorrFactor_Re и CorrFactor_Im. Итоговый критерий обнаружения считайте по модулю (или мощности):
 *  \(\text{TotalCorr}=\sqrt{\text{CorrFactor\_Re}^{2}+\text{CorrFactor\_Im}^{2}}\)
 *  Это сделает ваш приёмник абсолютно неубиваемым для частотных сдвигов в канале.
 *  ...
 *  но узявимым к шумам. Нужно поменять коррелятор и дифференциальнй детектор местами!
 */

#define SAMPLES_PER_CHIP  16
#define MLS_LEN           31
#define NUM_BLOCKS        4
#define RING_BUF_SIZE     1024  // Обязательно степень двойки!
#define RING_BUF_MASK     (RING_BUF_SIZE - 1)

// Опорный шаблон: абсолютная BPSK-фаза чипов на передаче (+1 или -1)
// Должен быть заполнен в соответствии с вашей спецификацией MLS-31
static const int8_t MLS_Ref[MLS_LEN] = {
    1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, -1,
    -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, -1, -1, 1, -1, 1
};

static const int8_t MLS_Diff_Ref[MLS_LEN] = {
//   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
     1,  1,  1,  1,  1, -1,  1, -1,  1, -1, -1, -1, -1,  1,  1, -1,
//  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30
     1,  1, -1, -1,  1, -1, -1, -1, -1,  1,  1,  1, -1, -1, -1
};

// Это массив АБСОЛЮТНЫХ знаков фазы, излучаемых вашим DDS передатчика
static const int8_t MLS_Absolute_Ref[31] = {
    1, 1, 1, 1, 1, -1, 1, 1, 1, -1, -1, 1, 1, 1, 1, -1,
    1, -1, -1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, -1, -1
};


void mls_sequence(void)
{
	// Выделяем память под дифференциальный шаблон
	static int8_t MLS_Diff_Ref[MLS_LEN];

    // РАСЧЕТ ДИФФЕРЕНЦИАЛЬНОГО ШАБЛОНА
    // Для первого чипа предыдущим считается пилот-тон (знак +1)
    int8_t last_sign = 1;

    for (int n = 0; n < MLS_LEN; n++) {
        MLS_Diff_Ref[n] = MLS_Ref[n] * last_sign;
        last_sign = MLS_Ref[n]; // Запоминаем текущий для следующего шага
    }
}

// Параметры секционирования (3 блока по 8 чипов, 1 блок из 7 чипов)
static const uint8_t block_chips[NUM_BLOCKS]   = {8, 8, 8, 7};
static const uint8_t block_offsets[NUM_BLOCKS] = {0, 8, 16, 24};


#define RINGBUF_PLUSSIZE		512
cplx_i16 RingBuffer[RING_BUF_SIZE] = { 0 }; // кольцевой буфер хранения выборок
uint16_t BufPtr = 0;

void mls_detector_init(void) {
    BufPtr = 0;
    for (int i = 0; i < RING_BUF_SIZE; i++) {
        RingBuffer[i].re = 0;
        RingBuffer[i].im = 0;
    }
}

// Функция, вызываемая на каждом новом входящем сэмпле (частота вызова 500 Гц)
uint32_t mls_detector_process(int16_t in_i, int16_t in_q) {
    // 1. Запись текущего сэмпла в буфер: HARD LIMIT
    RingBuffer[BufPtr].re = in_i;
    RingBuffer[BufPtr].im = in_q;

    uint32_t total_energy = 0;

    for (int b = 0; b < NUM_BLOCKS; b++)
    {
        int32_t accum_i = 0;
        int32_t accum_q = 0;

        int chips_in_block = block_chips[b];
        int start_chip = block_offsets[b];

        for (int c = 0; c < chips_in_block; c++)
        {
            int current_chip_global = start_chip + c;
            int8_t ref_sign = MLS_Absolute_Ref[current_chip_global];

            // Вычисляем строгий физический индекс начала чипа в истории буфера.
            // Самый старый чип (индекс 0) находится глубже всего в истории: 31 * 16 сэмплов назад.
            // Текущий чип (индекс 30) находится ближе всего к текущему моменту: 1 * 16 сэмплов назад.
            int16_t chip_history_depth = (MLS_LEN - current_chip_global) * SAMPLES_PER_CHIP;

            // Точка старта чипа в кольцевом буфере
            uint16_t chip_start_ptr = (BufPtr - chip_history_depth + 1 + RING_BUF_SIZE) & RING_BUF_MASK;

            // Интегрируем строго ВПЕРЕД во времени (от начала чипа к его концу)
            // Теперь все 16 сэмплов гарантированно принадлежат ОДНОМУ чипу
            if (ref_sign > 0) {
                for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
                    uint16_t ptr = (chip_start_ptr + s) & RING_BUF_MASK;
                    accum_i += RingBuffer[ptr].re;
                    accum_q += RingBuffer[ptr].im;
                }
            } else {
                for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
                    uint16_t ptr = (chip_start_ptr + s) & RING_BUF_MASK;
                    accum_i -= RingBuffer[ptr].re;
                    accum_q -= RingBuffer[ptr].im;
                }
            }
        }

        // Возведение в квадрат во фрейме int64_t защиты
        int64_t block_energy = ((int64_t)accum_i * accum_i) + ((int64_t)accum_q * accum_q);
        total_energy += (uint32_t)(block_energy >> 16); // Наш проверенный сдвиг
    }

    BufPtr = (BufPtr + 1) & RING_BUF_MASK;

    return total_energy;
}

#define SAMPLES_PER_CHIP  16
#define MLS_LEN           31
#define CHIP_BUF_SIZE     64
#define CHIP_BUF_MASK     (CHIP_BUF_SIZE - 1)

// Шаблон — ИСХОДНАЯ М-последовательность (управляет переворотом фазы)
static const int8_t MLS_Original_Ref[MLS_LEN] = {
    1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, -1,
    -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, -1, -1, 1, -1, 1
};

// Буферы памяти
static cplx_i16 SampleBuffer[SAMPLES_PER_CHIP]; // Всего 16 сэмплов!
static uint8_t  SamplePtr = 0;

static cplx_i32 ChipBuffer[CHIP_BUF_SIZE];      // Буфер комплексных чипов
static uint8_t  ChipPtr = 0;

static uint8_t  SampleCounter = 0;

/**
 * Вызывается на каждом входящем сэмпле (500 Гц)
 */
uint32_t mls_detector_process_v3(int16_t in_i, int16_t in_q)
{
    // 1. Копим сэмплы внутри текущего чипа
    SampleBuffer[SamplePtr].re = in_i;
    SampleBuffer[SamplePtr].im = in_q;
    SamplePtr = (SamplePtr + 1) % SAMPLES_PER_CHIP;

    SampleCounter++;

    // Коррелятор выдает новое значение только раз в 16 сэмплов (в конце каждого чипа)
    // На промежуточных сэмплах возвращаем 0 или предыдущее значение
    if (SampleCounter < SAMPLES_PER_CHIP) {
        return 0;
    }
    SampleCounter = 0; // Чип сформирован!

    // 2. Когерентно интегрируем накопленный чип
    int32_t chip_i = 0;
    int32_t chip_q = 0;
    for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
        chip_i += SampleBuffer[s].re;
        chip_q += SampleBuffer[s].im;
    }

    // Сохраняем интегрированный чип в буфер истории чипов
    ChipBuffer[ChipPtr].re = chip_i;
    ChipBuffer[ChipPtr].im = chip_q;

    // 3. Дифференциальное перемножение чипов и корреляция
    int32_t corr_accum_i = 0;
    int32_t corr_accum_q = 0;

    for (int c = 0; c < MLS_LEN; c++) {
        // Исходный знак М-последовательности
        int8_t ref_sign = MLS_Original_Ref[c];

        // Индексы текущего чипа и предыдущего чипа в истории
        // Двигаемся из глубины истории (c=0 — самый старый чип преамбулы) к настоящему времени
        uint8_t idx_curr = (ChipPtr - (MLS_LEN - c) + 1 + CHIP_BUF_SIZE) & CHIP_BUF_MASK;
        uint8_t idx_prev = (idx_curr - 1 + CHIP_BUF_SIZE) & CHIP_BUF_MASK;

        cplx_i32 ch_curr = ChipBuffer[idx_curr];
        cplx_i32 ch_prev = ChipBuffer[idx_prev];

        // Комплексное сопряженное перемножение соседних чипов: Y = Curr * conj(Prev)
        // Интенсивно поднимает SNR, убирает постоянную фазу несущей!
        int64_t diff_i = ((int64_t)ch_curr.re * ch_prev.re) + ((int64_t)ch_curr.im * ch_prev.im);
        int64_t diff_q = ((int64_t)ch_curr.im * ch_prev.re) - ((int64_t)ch_curr.re * ch_prev.im);

        // Масштабируем, чтобы не вылететь за int32_t при суммировании по MLS
        int32_t diff_i_32 = (int32_t)(diff_i >> 18);
        int32_t diff_q_32 = (int32_t)(diff_q >> 18);

        // Умножаем результат дифференцирования на исходный шаблон преамбулы
        if (ref_sign > 0) {
            corr_accum_i += diff_i_32;
            corr_accum_q += diff_q_32;
        } else {
            corr_accum_i -= diff_i_32;
            corr_accum_q -= diff_q_32;
        }
    }

    // Инкремент указателя истории чипов
    ChipPtr = (ChipPtr + 1) & CHIP_BUF_MASK;

    // Нам больше не нужно разбивать коррелятор на 4 блока!
    // Дифференциальное перемножение чипов УЖЕ убрало влияние ионосферы между ними.
    // Поэтому берем честный полный квадрат модуля один раз.
    int64_t total_energy = ((int64_t)corr_accum_i * corr_accum_i) + ((int64_t)corr_accum_q * corr_accum_q);

    return (uint32_t)(total_energy >> 16);
}

uint32_t mls_detector_process_v3_1(int16_t in_i, int16_t in_q)
{
    RingBuffer[BufPtr].re = in_i;
    RingBuffer[BufPtr].im = in_q;

    uint32_t total_energy = 0;

    // Вспомогательные переменные для вычисления среднего по блокам
    int32_t block_i[NUM_BLOCKS] = {0};
    int32_t block_q[NUM_BLOCKS] = {0};
    int32_t mean_i = 0;
    int32_t mean_q = 0;

    // 1. Когерентно копим блоки с шаблоном АБСОЛЮТНОЙ фазы (как в успешном тесте)
    for (int b = 0; b < NUM_BLOCKS; b++)
    {
        int chips_in_block = block_chips[b];
        int start_chip = block_offsets[b];

        for (int c = 0; c < chips_in_block; c++)
        {
            int current_chip_global = start_chip + c;
            int8_t ref_sign = MLS_Absolute_Ref[current_chip_global];

            int16_t chip_history_depth = (MLS_LEN - current_chip_global) * SAMPLES_PER_CHIP;
            uint16_t chip_start_ptr = (BufPtr - chip_history_depth + 1 + RING_BUF_SIZE) & RING_BUF_MASK;

            if (ref_sign > 0) {
                for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
                    uint16_t ptr = (chip_start_ptr + s) & RING_BUF_MASK;
                    block_i[b] += RingBuffer[ptr].re;
                    block_q[b] += RingBuffer[ptr].im;
                }
            } else {
                for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
                    uint16_t ptr = (chip_start_ptr + s) & RING_BUF_MASK;
                    block_i[b] -= RingBuffer[ptr].re;
                    block_q[b] -= RingBuffer[ptr].im;
                }
            }
        }
        // Суммируем для подсчета среднего DC-смещения преамбулы в данный момент
        mean_i += block_i[b];
        mean_q += block_q[b];
    }

    // Вычисляем среднее значение DC (сдвиг >> 2 равен делению на 4 блока)
    mean_i = mean_i >> 2;
    mean_q = mean_q >> 2;

    // 2. Некогерентно складываем энергии блоков, ОЧИЩЕННЫЕ от DC-компоненты пилота
    for (int b = 0; b < NUM_BLOCKS; b++)
    {
        // Вычитаем среднее: если это пилот-тон, результат станет равен 0!
        int32_t clean_i = block_i[b] - mean_i;
        int32_t clean_q = block_q[b] - mean_q;

        int64_t block_energy = ((int64_t)clean_i * clean_i) + ((int64_t)clean_q * clean_q);

        // Безопасный сдвиг >> 16 для предотвращения переполнения uint32_t при SNR=+96дБ
        total_energy += (uint32_t)(block_energy >> 16);
    }

    BufPtr = (BufPtr + 1) & RING_BUF_MASK;

    return total_energy;
}

// Дифференциальный шаблон для почипового метода (уничтожает пилот-тон и собирает пик)
static const int8_t MLS_Double_Diff_Ref[MLS_LEN] = {
    1,  1,  1,  1,  1, -1,  1, -1,  1, -1, -1, -1, -1,  1,  1, -1,
    1,  1, -1, -1,  1, -1, -1, -1, -1,  1,  1,  1, -1, -1, -1
};

uint32_t mls_detector_process_v3_fixed(int16_t in_i, int16_t in_q)
{
    // 1. Накопление сэмплов внутри чипа (16 сэмплов)
    SampleBuffer[SamplePtr].re = in_i;
    SampleBuffer[SamplePtr].im = in_q;
    SamplePtr = (SamplePtr + 1) % SAMPLES_PER_CHIP;

    SampleCounter++;
    if (SampleCounter < SAMPLES_PER_CHIP) {
        return 0; // Ждем окончания чипа
    }
    SampleCounter = 0;

    // Когерентный интеграл чипа
    int32_t chip_i = 0, chip_q = 0;
    for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
        chip_i += SampleBuffer[s].re;
        chip_q += SampleBuffer[s].im;
    }

    ChipBuffer[ChipPtr].re = chip_i;
    ChipBuffer[ChipPtr].im = chip_q;

    // 2. Дифференциальное перемножение чипов и корреляция
    int32_t corr_accum_i = 0;
    int32_t corr_accum_q = 0;

    for (int c = 0; c < MLS_LEN; c++)
    {
        // Используем двойной дифференциальный шаблон
        int8_t ref_sign = MLS_Double_Diff_Ref[c];

        uint8_t idx_curr = (ChipPtr - (MLS_LEN - c) + 1 + CHIP_BUF_SIZE) & CHIP_BUF_MASK;
        uint8_t idx_prev = (idx_curr - 1 + CHIP_BUF_SIZE) & CHIP_BUF_MASK;

        cplx_i32 ch_curr = ChipBuffer[idx_curr];
        cplx_i32 ch_prev = ChipBuffer[idx_prev];

        // Комплексное сопряженное перемножение соседних чипов
        int64_t diff_i = ((int64_t)ch_curr.re * ch_prev.re) + ((int64_t)ch_curr.im * ch_prev.im);
        int64_t diff_q = ((int64_t)ch_curr.im * ch_prev.re) - ((int64_t)ch_curr.re * ch_prev.im);

        // КРИТИЧЕСКИЙ СДВИГ >> 19: Полностью защищает от переполнения при SNR = +96 дБ
        int32_t diff_i_32 = (int32_t)(diff_i >> 20);
        int32_t diff_q_32 = (int32_t)(diff_q >> 20);

        if (ref_sign > 0) {
            corr_accum_i += diff_i_32;
            corr_accum_q += diff_q_32;
        } else {
            corr_accum_i -= diff_i_32;
            corr_accum_q -= diff_q_32;
        }
    }

    ChipPtr = (ChipPtr + 1) & CHIP_BUF_MASK;

    // Финальный расчет энергии (полная когерентная база 31 чип!)
    int64_t total_energy = ((int64_t)corr_accum_i * corr_accum_i) + ((int64_t)corr_accum_q * corr_accum_q);

    // Масштабируем для вывода на график
    return (uint32_t)(total_energy);
}

uint32_t gps_style_search(int16_t in_i, int16_t in_q);

//#define SAMPLES_PER_CHIP  16
#define MLS63_LEN         63
//#define NUM_BLOCKS        4
//#define RING_BUF_SIZE     1024 // Степень двойки под MLS-63
//#define RING_BUF_MASK     (RING_BUF_SIZE - 1)

// Длины блоков в чипах для MLS-63
static const uint8_t block_chips63[NUM_BLOCKS]   = {16, 16, 16, 15};
static const uint8_t block_offsets63[NUM_BLOCKS] = {0, 16, 32, 48};

// Массив АБСОЛЮТНЫХ знаков фазы вашей новой MLS-63 (замените на вашу актуальную после генерации)
static const int8_t MLS63_Absolute_Ref[MLS63_LEN] = {
	    1, 1, 1, 1, 1, -1, 1, 1, 1, 1, -1, -1, 1, 1, 1, -1,
	    1, -1, 1, 1, -1, -1, -1, -1, 1, -1, 1, 1, 1, -1, -1, -1,
	    1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, -1, -1, 1, -1, -1,
	    1, 1, -1, -1, 1, -1, 1, -1, 1, -1, -1, -1, -1, -1, -1
};

static const int8_t MLS63_Ref [MLS63_LEN] = {
	    1, 1, 1, 1, 1, -1, 1, 1, 1, 1, -1, -1, 1, 1, 1, -1,
	    1, -1, 1, 1, -1, -1, -1, -1, 1, -1, 1, 1, 1, -1, -1, -1,
	    1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, -1, -1, 1, -1, -1,
	    1, 1, -1, -1, 1, -1, 1, -1, 1, -1, -1, -1, -1, -1, -1
};

uint32_t mls63_segmented_detector(int16_t in_i, int16_t in_q)
{
    // Запись линейного зашумленного сигнала напрямую в буфер
    RingBuffer[BufPtr].re = in_i;
    RingBuffer[BufPtr].im = in_q;

    uint32_t total_energy = 0;

    // Считаем 4 блока
    for (int b = 0; b < NUM_BLOCKS; b++)
    {
        int32_t accum_i = 0;
        int32_t accum_q = 0;

        int chips_in_block = block_chips63[b];
        int start_chip = block_offsets63[b];

        for (int c = 0; c < chips_in_block; c++)
        {
            int current_chip_global = start_chip + c;
            int8_t ref_sign = MLS63_Absolute_Ref[current_chip_global];

            // Строгое позиционирование чипа в истории буфера (вперед во времени)
            int16_t chip_history_depth = (MLS63_LEN - current_chip_global) * SAMPLES_PER_CHIP;
            uint16_t chip_start_ptr = (BufPtr - chip_history_depth + 1 + RING_BUF_SIZE) & RING_BUF_MASK;

            if (ref_sign > 0) {
                for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
                    uint16_t ptr = (chip_start_ptr + s) & RING_BUF_MASK;
                    accum_i += RingBuffer[ptr].re;
                    accum_q += RingBuffer[ptr].im;
                }
            } else {
                for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
                    uint16_t ptr = (chip_start_ptr + s) & RING_BUF_MASK;
                    accum_i -= RingBuffer[ptr].re;
                    accum_q -= RingBuffer[ptr].im;
                }
            }
        }

        // Переводим накопленную амплитуду блока в энергию (мощность)
        int64_t block_energy = ((int64_t)accum_i * accum_i) + ((int64_t)accum_q * accum_q);

        // Сдвиг >> 18 для предотвращения переполнения uint32_t (так как база выросла в 2 раза)
        total_energy += (uint32_t)(block_energy >> 18);
    }

    BufPtr = (BufPtr + 1) & RING_BUF_MASK;

    return total_energy;
}

uint32_t mls63_fully_coherent_detector(int16_t in_i, int16_t in_q)
{
    RingBuffer[BufPtr].re = in_i;
    RingBuffer[BufPtr].im = in_q;

    int32_t accum_i = 0;
    int32_t accum_q = 0;

    // Один сплошной когерентный цикл по всей длине MLS-63
    for (int c = 0; c < MLS63_LEN; c++)
    {
        int8_t ref_sign = MLS63_Absolute_Ref[c];

        int16_t chip_history_depth = (MLS63_LEN - c) * SAMPLES_PER_CHIP;
        uint16_t chip_start_ptr = (BufPtr - chip_history_depth + 1 + RING_BUF_SIZE) & RING_BUF_MASK;

        if (ref_sign > 0) {
            for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
                uint16_t ptr = (chip_start_ptr + s) & RING_BUF_MASK;
                accum_i += RingBuffer[ptr].re;
                accum_q += RingBuffer[ptr].im;
            }
        } else {
            for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
                uint16_t ptr = (chip_start_ptr + s) & RING_BUF_MASK;
                accum_i -= RingBuffer[ptr].re;
                accum_q -= RingBuffer[ptr].im;
            }
        }
    }

    // Квадрат берется ОДИН раз на выходе всей последовательности
    int64_t total_coherent_energy = ((int64_t)accum_i * accum_i) + ((int64_t)accum_q * accum_q);

    BufPtr = (BufPtr + 1) & RING_BUF_MASK;

    return (uint32_t)(total_coherent_energy >> 18);
}

static int8_t MLS63_Diff_Ref[MLS63_LEN];


uint32_t mls63_coherent_chip_diff_detector(int16_t in_i, int16_t in_q)
{
    // Накопление сэмплов внутри чипа
    SampleBuffer[SamplePtr].re = in_i;
    SampleBuffer[SamplePtr].im = in_q;
    SamplePtr = (SamplePtr + 1) % SAMPLES_PER_CHIP;

    SampleCounter++;
    if (SampleCounter < SAMPLES_PER_CHIP) {
        return 0; // Ждем накопления полного чипа (вызов коррелятора раз в 16 сэмплов)
    }
    SampleCounter = 0;

    // Когерентный интеграл текущего чипа
    int32_t chip_i = 0, chip_q = 0;
    for (int s = 0; s < SAMPLES_PER_CHIP; s++) {
        chip_i += SampleBuffer[s].re;
        chip_q += SampleBuffer[s].im;
    }

    ChipBuffer[ChipPtr].re = chip_i;
    ChipBuffer[ChipPtr].im = chip_q;

    // Сплошная когерентная корреляция по продифференцированным чипам
    int32_t corr_accum_i = 0;
    int32_t corr_accum_q = 0;

    // Идем по 62 дифференциальным парам внутри MLS-63
    for (int c = 1; c < MLS63_LEN; c++)
    {
        // ВАЖНО: берем знак из исходной MLS (так как передатчик кодировал переходы)
        //int8_t ref_sign = MLS63_Absolute_Ref[c];
    	int8_t ref_sign = MLS63_Diff_Ref[c];

        // Извлекаем чипы из истории назад во времени
        uint8_t idx_curr = (ChipPtr - (MLS63_LEN - 1 - c) + CHIP_BUF_SIZE) & CHIP_BUF_MASK;
        uint8_t idx_prev = (idx_curr - 1 + CHIP_BUF_SIZE) & CHIP_BUF_MASK;

        cplx_i32 ch_curr = ChipBuffer[idx_curr];
        cplx_i32 ch_prev = ChipBuffer[idx_prev];

        // Комплексное дифференцирование чипов: Y = Curr * conj(Prev)
        int64_t diff_i = ((int64_t)ch_curr.re * ch_prev.re) + ((int64_t)ch_curr.im * ch_prev.im);
        int64_t diff_q = ((int64_t)ch_curr.im * ch_prev.re) - ((int64_t)ch_curr.re * ch_prev.im);

        // Безопасный сдвиг >> 18 для предотвращения переполнения
        int32_t diff_i_32 = (int32_t)(diff_i >> 24);
        int32_t diff_q_32 = (int32_t)(diff_q >> 24);

        if (ref_sign > 0) {
            corr_accum_i += diff_i_32;
            corr_accum_q += diff_q_32;
        } else {
            corr_accum_i -= diff_i_32;
            corr_accum_q -= diff_q_32;
        }
    }

    ChipPtr = (ChipPtr + 1) & CHIP_BUF_MASK;

    // Полный квадрат модуля когерентной суммы
    int64_t total_energy = ((int64_t)corr_accum_i * corr_accum_i) + ((int64_t)corr_accum_q * corr_accum_q);

    return (uint32_t)(total_energy);
}

#include <stdlib.h>
#include <math.h>
void PhaseDiscriminatorTest(void) {

	uint16_t DelayedPtr = 16;
	uint16_t CorrPtr = 0;
	cplx_i16 DkAvg = {.re=0, .im=0}; // тупой усреднитель

	qshort_channel_sim_t ChannelSimulator;
	channel_sim_init(&ChannelSimulator, -2.5, 0.0, 0.0, 0.0, 8000);

	// 2000 сэмплов без модуляции, затем символы дифф.MLS-31 (их 30!) и ещё 2000 сэмплов
	int SignalLength = 20000 + (256*(MLS63_LEN-1)) + 2000;
	int n = 0; // индекс по дифф.символам MLS
	int m = 0; // счётчик сэмплов

	DDS_Core_t DDS_TestSignal;
	DDS_Core_t DDS_LO;
	FILE *dump = fopen("DDS_Dump.csv", "wt");
	//PrepMLS31DiffTemplate();
	PrepMLSTx();
	PrepMLSRxTemplate();

	DDS_Init (&DDS_TestSignal, 8000, 1000);
	DDS_Init (&DDS_LO, 8000, 1000);
	for (int i = 0; i < SignalLength; i++) { // 2 seconds
		cplx_i16 TestSignlSample, IF_Out;
		DDS_Tick (&DDS_TestSignal, &TestSignlSample);

		/* изменение фазы = бит 1*/
		/* нет изменения = бит 0 */
		if ((n<63)&&(i > 10000)) { /* стартовый период без модуляции завершился и последовательность ещё не закончилась */
			if (m == 0) { /* начало символа */
				if ((MLS63_Ref[n] == -1)) { // надо повернуть?
					//DDS_RotatePhase (&DDS_TestSignal, +180); // поворачиваем!
					DDS_SetPhaseAbsolute (&DDS_TestSignal, 180);
				} else {
					DDS_SetPhaseAbsolute (&DDS_TestSignal, 0);
				}
			};
			m++;
			if (m == 256) {
				m = 0; n++;
			};
		}

		cplx_f32 in_sample = {.re = (float)TestSignlSample.re/(float)34000, .im =(float)TestSignlSample.im/(float)34000};
		cplx_f32 out_sample;
		channel_sim_process(&ChannelSimulator, &in_sample, &out_sample);
		TestSignlSample.re = (int16_t)(out_sample.re * 32000);
		TestSignlSample.im = (int16_t)(out_sample.im * 32000);

		/*if (i==15000) {
			int64_t tmpCorr = (int64_t)diff_corr_buff_re[0] * mls_31_diff_template_corr[0];
			printf ("i = %d; diff_corr_buff[0] = %d; mls_31_diff_template_corr[0] = %d; mult = %lld\n",
					i, diff_corr_buff_re[0], mls_31_diff_template_corr[0], tmpCorr);
		}*/

		if (process_sample_downconvert_and_decimate (TestSignlSample, &DDS_LO, &IF_Out)) { // Zero-IF, теперь работаем тут
#if 0
		    // 1. Сохраняем в кольцевой буфер (размер RINGBUFSIZE должен быть заведомо больше 16, например 32 или 64)
		    RingBuffer[BufPtr] = IF_Out;


		    // 2. Дифференциальное перемножение (задержка ровно 16 сэмплов)
		    // Такая схема практически неработоспособна при SNR < +3дБ
		    // Ищем выход: когерентное накопление?
		    cplx_i32 Dk = cplx_i32_mul_conj(RingBuffer[BufPtr], RingBuffer[DelayedPtr]);

		    // Шаг инкремента указателей
		    if (++BufPtr >= RINGBUFSIZE) BufPtr = 0;
		    if (++DelayedPtr >= RINGBUFSIZE) DelayedPtr = 0;

		    // 3. Сохраняем "мягкое" значение в буфер коррелятора.
		    // НЕ превращаем в +1/-1! Сохраняем как есть (можно немного сжать по разрядности, например >> 12 или >> 16)
		    // ЛИНЕЙНЫЙ СДВИГ БУФЕРА (вместо кольцевого CorrPtr)
		    // Сдвигаем все элементы на один шаг назад. Самый старый (индекс 0) вылетает.
		    for (int cidx = 0; cidx < CORR_TEMPLATE_LENGTH - 1; cidx++) {
		        diff_corr_buff_re[cidx] = diff_corr_buff_re[cidx + 1];
		        diff_corr_buff_im[cidx] = diff_corr_buff_im[cidx + 1];
		    }
		    // Новый сэмпл всегда падает в самый конец буфера!
		    diff_corr_buff_re[CORR_TEMPLATE_LENGTH - 1] = Dk.re;
		    diff_corr_buff_im[CORR_TEMPLATE_LENGTH - 1] = Dk.im;

		    // ТЕПЕРЬ КОРРЕЛЯЦИЯ СЧИТАЕТСЯ СТРОГО ЛИНЕЙНО
		    // Шаблон и буфер идеально сонаправлены во времени
		    int32_t t_ptr = CorrPtr; // Стартуем со старого сэмпла
		    int64_t CorrFactor_Re = 0;
		    int64_t CorrFactor_Im = 0;

		    for (int cidx = 0; cidx < CORR_TEMPLATE_LENGTH; cidx++) {
		    	CorrFactor_Re += (int64_t)diff_corr_buff_re[t_ptr] * mls_31_diff_template_corr[cidx];
		    	CorrFactor_Im += (int64_t)diff_corr_buff_im[t_ptr] * mls_31_diff_template_corr[cidx];

		        // Двигаем указатель кольцевого буфера вперед
		        if (++t_ptr >= CORR_TEMPLATE_LENGTH) {
		            t_ptr = 0;
		        }
		    }
	        /*if (++CorrPtr >= CORR_TEMPLATE_LENGTH) {
	        	CorrPtr = 0;
	        }*/
		    double scaled_down_CFRe = (double)(CorrFactor_Re );
		    double scaled_down_CFIm = (double)(CorrFactor_Im );
		    uint64_t TotalCorrFactor = (uint64_t)sqrtl((scaled_down_CFRe * scaled_down_CFRe) + (scaled_down_CFIm * scaled_down_CFIm));

		    // Пишем лог. Ищем глазами огромный всплеск (пик) CorrFactorRe!
		    fprintf (dump, "%d, %ld, %ld\n", i, Dk.re*1L, TotalCorrFactor);
#else
		    // Шаг инкремента указателей
		    uint32_t total_energy = mls63_fully_coherent_detector (IF_Out.re, IF_Out.im);
		    fprintf (dump, "%d, %ld, %ld, %lu\n", i, IF_Out.re*1L, IF_Out.im*1L, (long unsigned int)total_energy);
#endif
		}
	}
	fclose(dump);
}

#define SAMPLES_PER_CHIP  16
#define MLS_LEN           31
#define TOTAL_SAMPLES     (MLS_LEN * SAMPLES_PER_CHIP) // 496 сэмплов
//#define RING_BUF_SIZE     512
#define RING_BUF_MASK     (RING_BUF_SIZE - 1)
#define NUM_FREQ_CHANNELS 5 // 5 каналов поиска по частоте

// Таблица поворота фазы для компенсации Доплера (заполняется при старте)
// LUT_cos и LUT_sin хранят значения, масштабированные в Fixed-Point (например, коэфф. 16384)
static int16_t Freq_LUT_Cos[NUM_FREQ_CHANNELS][TOTAL_SAMPLES];
static int16_t Freq_LUT_Sin[NUM_FREQ_CHANNELS][TOTAL_SAMPLES];

// Опорный шаблон: АБСОЛЮТНЫЕ знаки фазы передатчика (как в нашем первом успешном тесте!)

void Freq_LUT_init (void) {
	for (int i=0; i< NUM_FREQ_CHANNELS; i++) {
		for (int j=0; j<TOTAL_SAMPLES; j++) {
			Freq_LUT_Cos[i][j] = 16384;
			Freq_LUT_Sin[i][j] = 0;
		}
	}

	// В функции init:
	int8_t last_s = 1;
	for (int c = 0; c < MLS63_LEN; c++) {
	    MLS63_Diff_Ref[c] = MLS63_Absolute_Ref[c] * last_s;
	    last_s = MLS63_Absolute_Ref[c];
	}

}

uint32_t gps_style_search(int16_t in_i, int16_t in_q)
{
    // Буфер хранит чистый линейный сигнал (без Hard Limit и без дифференцирования)
    RingBuffer[BufPtr].re= in_i;
    RingBuffer[BufPtr].im = in_q;

    uint32_t max_energy_across_channels = 0;

    // Цикл по всем частотным каналам поиска
    for (int f = 0; f < NUM_FREQ_CHANNELS; f++)
    {
        int32_t accum_i = 0;
        int32_t accum_q = 0;

        // Линейный проход по всей длине преамбулы (496 сэмплов назад в историю)
        for (int c = 0; c < MLS_LEN; c++)
        {
            int8_t ref_sign = MLS_Absolute_Ref[c];

            // Глубина залегания текущего чипа в буфере истории
            int16_t sample_offset = (MLS_LEN - c) * SAMPLES_PER_CHIP;
            uint16_t chip_start_ptr = (BufPtr - sample_offset + 1 + RING_BUF_SIZE) & RING_BUF_MASK;

            for (int s = 0; s < SAMPLES_PER_CHIP; s++)
            {
                uint16_t ptr = (chip_start_ptr + s) & RING_BUF_MASK;
                int16_t si = RingBuffer[ptr].re;
                int16_t sq = RingBuffer[ptr].im;

                // Номер текущего сэмпла внутри всей преамбулы (0...495)
                int sample_idx = c * SAMPLES_PER_CHIP + s;

                // 1. Извлекаем поворот фазы для данного частотного канала из таблицы
                int16_t cos_val = Freq_LUT_Cos[f][sample_idx];
                int16_t sin_val = Freq_LUT_Sin[f][sample_idx];

                // 2. Поворачиваем фазу входящего сэмпла (Комплексное умножение в Fixed-Point)
                // Результат сдвигаем на 14 бит (масштаб таблицы)
                int32_t rotated_i = ((int32_t)si * cos_val - (int32_t)sq * sin_val) >> 14;
                int32_t rotated_q = ((int32_t)sq * cos_val + (int32_t)si * sin_val) >> 14;

                // 3. Когерентно накапливаем с учетом знака преамбулы
                if (ref_sign > 0) {
                    accum_i += rotated_i;
                    accum_q += rotated_q;
                } else {
                    accum_i -= rotated_i;
                    accum_q -= rotated_q;
                }
            }
        }

        // Вычисляем энергию для текущего частотного канала
        int64_t channel_energy = ((int64_t)accum_i * accum_i) + ((int64_t)accum_q * accum_q);
        uint32_t energy_32 = (uint32_t)(channel_energy >> 16); // Защита от переполнения

        // Фиксируем максимум среди всех частотных каналов
        if (energy_32 > max_energy_across_channels) {
            max_energy_across_channels = energy_32;
        }
    }

    BufPtr = (BufPtr + 1) & RING_BUF_MASK;
    return max_energy_across_channels;
}


int main (int argc, char *argv[]) {
	//viterbi_table_gen();
	//viterbi_ack_test();
	//rs_config_t cfg;
	gf_init(); // иначе табличные функции не работают!
	//rs_init_geometry_x(26, 20);
    /*for (int i = 0; i <= cfg.t2; i++) {
        printf("0x%02X, ", cfg.gen_poly[i]);
    }*/

	RS_Payload_Ack_t Ack = { .DATA = { 0xBA, 0xAD, 0xBA, 0xBE, 0x55, 0xAA, 0x13, 0x37 }};
	RS_Payload_Data_t Data = { .DATA = { 0xBA, 0xAD, 0xBA, 0xBE, 0x55, 0xAA, 0x13, 0x37 }};

	int errn;

	rs_encode_fast(&rs_config_ack_14_8, Ack.DATA, Ack.RS); // для пакетов типа ACK
	rs_encode_fast(&rs_config_data_26_20, Data.DATA, Data.RS); // для пакетов типа DATA

	// ломаем байты!
	Ack.ENCODED[1] = 0xFF;
	Ack.ENCODED[7] = 0xFF;
	Ack.ENCODED[8] = 0xFF;

	// fubar
	//Ack.ENCODED[11] = 0xFF;

	if ((errn = rs_decode_fast (&rs_config_ack_14_8, Ack.ENCODED)) < 0) {
		printf ("Data is fubar\n");
	} else if (errn > 0) {
		printf ("Data is recovered, errn: %d\n", errn);
	} else {
		printf ("No RS errors detected\n");
	}
	printf (" Source: ");
	for (int i = 0; i < sizeof(Ack.DATA); i++) {
		printf ("%02X ", Ack.DATA[i]);
	}
	printf ("\n");
	printf ("Encoded: ");
	for (int i = 0; i < sizeof(Ack.DATA); i++) {
		printf ("%02X ", Ack.ENCODED[i]);
	}
	printf ("\n");

	Freq_LUT_init();

	PhaseDiscriminatorTest();

	return 0;
}

static int16_t SinTab[256]; // ToDo: в боевой системе это прекомпилированная таблица во флэш, в модели - просто массив.
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

