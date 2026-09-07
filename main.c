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
#include "complex_math.h"
#include "trx.h"
#include "scrambler.h"
/* берём указатель на массив байтов,
  проходим по нему скрэмблером,
  возвращаем скрэмблированное значение текущего дибита.
  Трактуем index как именно индекс ДИБИТОВ!
  Возвращаем 0, если данные ещё есть, возвращаем 1, если прошли по всему массиву.
  перед первым запуском итератора (index==0)сбрасываем скрэмблер автоматически */
int scramble_iterate (const uint8_t *src, uint16_t BytesTotal, uint16_t *index, uint8_t *scrambled) {
	uint16_t ByteN = (*index >> 2);
	uint8_t DibitN = (uint8_t)(*index & 0x03);
	static uint8_t CurrentByte;
	static uint8_t lfsr;
	if (*index == 0) {
		scrambler_reset(&lfsr);
		printf ("[LFSR := %02X] ", lfsr);
	}
	if (DibitN == 0) {
		/* если мы в начале байта - извлекаем и скрэмблируем его сразу */
		CurrentByte = scrambler_process_byte (&lfsr, src[ByteN]);
		printf ("[Scramle %02X -> %02X] ", src[ByteN], CurrentByte);
	}
	*scrambled = (CurrentByte >> (DibitN<<1)) & 0x03;
	*index += 1;
	printf ("Byte #%u, dibit #%u; ", ByteN, DibitN);
	return (((ByteN == (BytesTotal-1)) && (DibitN == 3)));
}

/**
 * @brief Итератор дескремблера на стороне приемника
 * @param scrambled_dibit Текущий дибит от демодулятора (0..3)
 * @param index Индекс принятых дибитов (передается по указателю)
 * @param dst_buffer Указатель на массив, куда складывать очищенные байты данных
 * @return int Возвращает 1, если собран очередной байт и положен в dst_buffer, иначе 0
 */
int descramble_iterate(uint8_t scrambled_dibit, uint16_t *index, uint8_t *dst_buffer) {
    uint16_t ByteN = (*index >> 2);
    uint8_t DibitN = (uint8_t)(*index & 0x03);
    static uint8_t AssembledByte = 0;
    static uint8_t lfsr;

    // Автоматический сброс LFSR при старте нового пакета
    if (*index == 0) {
        scrambler_reset(&lfsr);
        AssembledByte = 0;
    }

    // Собираем байт из дибитов «на лету»
    // Вставляем 2 бита в нужную позицию байта (зеркально сдвигам в передатчике)
    AssembledByte |= ((scrambled_dibit & 0x03) << (DibitN << 1));

    *index += 1;

    // Если мы заполнили последний дибит байта (дибит #3)
    if (DibitN == 3) {
        // Прогоняем через тот же LFSR. Так как XOR обратим, мы получим исходный байт!
        dst_buffer[ByteN] = scrambler_process_byte(&lfsr, AssembledByte);

        // Сбрасываем накопитель для следующего байта
        AssembledByte = 0;
        return 1; // Сигнализируем, что байт готов
    }

    return 0; // Байт еще не собран полностью
}

void test_scramble_iterate(void) {
	uint8_t TestData[] = { 0xAA, 0x55, 0x12 };
	uint8_t RestoredData[3] = {0};
	uint32_t Length = sizeof(TestData);
	uint16_t tx_idx = 0;
	uint16_t rx_idx = 0;
	uint8_t scrambled_dibit = 0;
	int done = 0;

	printf ("Start of test_scramble_iterate()\n");
	int tx_done = 0;
	while (!tx_done) {
	    // 1. Передатчик выдает один дибит
	    tx_done = scramble_iterate(TestData, 3, &tx_idx, &scrambled_dibit);

	    // 2. Приемник тут же его забирает
	    if (descramble_iterate(scrambled_dibit, &rx_idx, RestoredData)) {
	        printf(" -> [Байт восстановлен!]\n");
	    }
	}

	// Проверяем результат
	if (TestData[0] == RestoredData[0] && TestData[1] == RestoredData[1] && TestData[2] == RestoredData[2]) {
	    printf("Ура! Цепочка скремблер->дескремблер работает идеально без потерь данных.\n");
	} else {
	    printf("Ошибка! Данные не совпали.\n");
	}
	printf ("End of test_scramble_iterate()\n");
}

void DDS_Test(void) {
	wav_stream_t WavDump;
	uint32_t total_samples = 0;
	cplx_f32 sample = { 0 };
	tx_machine_t my_tx;
	uint8_t TestData[] = {0x18, 0x38, 0x18, 0x38, 0x18, 0x38, 0x18, 0x38, 0x18, 0x38, 0x18, 0x38 };
	uint32_t Length = sizeof(TestData);

	init_dds_table();
	tx_machine_start(&my_tx, TestData, Length);

	wav_open_write (&WavDump, "test.wav");
	while (tx_machine_process_sample(&my_tx, &sample.re, &sample.im)) {
		wav_write_sample (&WavDump, &sample);
		total_samples++;
	}

	wav_close(&WavDump);
}

#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

FILE *csv;

void rx_Search_Preamble_test(void) {
	wav_stream_t WavSource;
	uint32_t total_samples = 0;
	cplx_f32 sample = { 0 };
	tx_machine_t my_tx;
	uint8_t TestData[] = {0x0, 0x0, 0x01, 0x0, 0x0, 0x0, 0x01, 0x0, 0x0f, 0x0, 0x0, 0x0, 0x0, 0x01, 0x0, 0x0 };
	uint32_t Length = sizeof(TestData);
	bool TestResult = false, Success = false;
	const char* Filname = "test.wav";
	int supress_output = 0;

	init_dds_table();
	rx_sync_t sync;
	//rx_sync_init(&sync);
	rx_lo_t lo;
	rx_lo_init(&lo);

	if (wav_open_read (&WavSource, Filname)) {
		printf ("\n\nТест rx_search_pilot_efficient() начинается.\n");
		 csv = fopen("sync_debug.csv", "wt");
		 fprintf(csv, "SymbolIndex,Correlation,State\n");
	} else {
		printf ("\n\nВНИМАНИЕ! Для теста rx_search_pilot_efficient() нужен %s.\n", Filname);
		return;
	}
	while (wav_read_sample(&WavSource, &sample)) {
		float out_dc_power;
		if ((TestResult = rx_search_pilot_efficient (&lo, sample, &out_dc_power))) {
			if (!supress_output) printf ("Пилот-тон обнаружен, сэмпл %lu\n", total_samples);
			Success = true;
			supress_output = 1;
		}
		total_samples++;
	}
	printf ("Тест rx_search_pilot_efficient(): %s.\nПросмотрено сэмплов: %lu\n",
			((Success) ? "завершён успешно" : "ПРОВАЛЕН! Пилот-тон не найден"),
			total_samples);
	wav_close(&WavSource);
	fclose(csv);
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

	DDS_Test();

	//test_scramble_iterate();
	rx_Search_Preamble_test();

	printf ("All tests are done\n");
	return 0;
}


