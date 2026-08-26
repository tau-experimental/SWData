#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>

// Временные структуры, если они еще не объявлены в config.h
typedef struct {
    float re;
    float im;
} complex_f;

#define FS 8000.0f
#define NUM_DATA_TONES 4
#define PI_F 3.1415926535f

// Внешние функции из rx.c, которые мы тестируем
extern void rx_filters_init(void);
extern void rx_filters_process_sample(const complex_f *in_sample);

// Доступ к внутренним структурам каналов для логирования тестовых метрик
typedef struct {
    float freq_target;
    float cos_w0;
    float sin_w0;
    complex_f y_prev;
    complex_f y_delayed;
    float cfo_error_hz;
    float phase_jump_metric;
    uint32_t hold_counter;
    bool is_switching;
} channel_filter_t;

extern channel_filter_t rx_channels[NUM_DATA_TONES];

// Генератор белого гауссова шума (метод Бокса-Мюллера)
static float generate_gaussian_noise(float sigma) {
    if (sigma <= 0.0f) return 0.0f;
    float u1 = (float)rand() / (float)RAND_MAX;
    float u2 = (float)rand() / (float)RAND_MAX;
    if (u1 < 1e-9f) u1 = 1e-9f; // защита от логарифма нуля
    return sigma * sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI_F * u2);
}

int main(void) {
    srand((unsigned int)time(NULL));

    // Инициализация тестируемого RX-каскада
    rx_filters_init();

    printf("\n============== ЗАПУСК СТРЕСС-ТЕСТА ФИЛЬТРОВ И ФАПЧ ==============\n");

    // ПАРАМЕТРЫ СТРЕСС-ТЕСТА
    const float sim_cfo_hz = 35.0f;     // Намеренная КВ-расстройка частоты (35 Гц)
    const float noise_sigma = 0.4f;     // Мощный шум в канале (SNR около 5-6 дБ)
    const int sim_duration_samples = 1200; // Общая длительность симуляции

    // Локальные фазы генераторов передатчика для 4-х тонов
    float tx_phases[NUM_DATA_TONES] = {0.0f, 0.0f, 0.0f, 0.0f};
    float tone_frequencies[NUM_DATA_TONES] = {1000.0f, 1200.0f, 1400.0f, 1600.0f};

    printf("[SIM] Входные условия: CFO = +%.1f Гц, Шум (Sigma) = %.2f\n", sim_cfo_hz, noise_sigma);
    printf("[SIM] На сэмпле 400 ломаем синхронность: искусственно сдвигаем фазы тонов!\n\n");

    // Основной цикл симуляции по сэмплам
    for (int n = 0; n < sim_duration_samples; n++) {
        complex_f tx_signal = {0.0f, 0.0f};

        // --- ИМИТАЦИЯ НАМЕРЕННОЙ ПОРЧИ СИГНАЛА НА ПЕРЕДАТЧИКЕ ---
        // На сэмпле 400 имитируем "честный" асинхронный скачок фазы
        if (n == 400) {
            printf("\n⚠️ [TX МАНЕВР] Сэмпл %d: Происходит независимый скачок фаз в каналах!\n", n);
            tx_phases[0] += PI_F / 2.0f;  // Тон 1: +90 градусов
            tx_phases[1] -= PI_F / 2.0f;  // Тон 2: -90 градусов
            tx_phases[2] += PI_F;         // Тон 3: 180 градусов (инверсия)
            tx_phases[3] += PI_F / 4.0f;  // Тон 4: +45 градусов
        }

        // Синтезируем групповой КВ-сигнал с учетом CFO
        for (int t = 0; t < NUM_DATA_TONES; t++) {
            // Реальная мгновенная частота тона в эфире с учетом расстройки CFO
            float real_freq = tone_frequencies[t] + sim_cfo_hz;
            float step = 2.0f * PI_F * real_freq / FS;

            tx_phases[t] += step;
            // Удерживаем фазу в пределах [-PI, PI] для точности float
            if (tx_phases[t] > PI_F)  tx_phases[t] -= 2.0f * PI_F;
            if (tx_phases[t] < -PI_F) tx_phases[t] += 2.0f * PI_F;

            // Накапливаем комплексную смесь 4-х тонов
            tx_signal.re += cosf(tx_phases[t]);
            tx_signal.im += sinf(tx_phases[t]);
        }

        // Нормализуем амплитуду суммы тонов, чтобы она не улетала в клиппинг
        tx_signal.re /= (float)NUM_DATA_TONES;
        tx_signal.im /= (float)NUM_DATA_TONES;

        // Добавляем аддитивный белый гауссов шум (AWGN) в I и Q каналы
        tx_signal.re += generate_gaussian_noise(noise_sigma);
        tx_signal.im += generate_gaussian_noise(noise_sigma);

        // --- ПРОПУСКАЕМ ЗАШУМЛЕННЫЙ СИГНАЛ ЧЕРЕЗ ФИЛЬТРЫ ПРИЕМНИКА ---
        rx_filters_process_sample(&tx_signal);

        // --- МОНИТОРИНГ И ЛОГИРОВАНИЕ ДИНАМИКИ ПОВЕДЕНИЯ ---
        // Выводим состояние метрик каждые 50 сэмплов для отслеживания стабильности
        if (n % 50 == 0 || n == 401 || n == 405 || n == 420) {
            printf("[%04d] CFO Оценка: T1=%+5.1fГц | T2=%+5.1fГц | T3=%+5.1fГц | T4=%+5.1fГц  ",
                   n,
                   rx_channels[0].cfo_error_hz,
                   rx_channels[1].cfo_error_hz,
                   rx_channels[2].cfo_error_hz,
                   rx_channels[3].cfo_error_hz);

            // Вывод флагов детекции переключения фаз (Hold-окно)
            printf("Флаги скачка: [%c%c%c%c]\n",
                   rx_channels[0].is_switching ? 'X' : '.',
                   rx_channels[1].is_switching ? 'X' : '.',
                   rx_channels[2].is_switching ? 'X' : '.',
                   rx_channels[3].is_switching ? 'X' : '.');
        }
    }

    printf("\n============== ТЕСТ ЗАВЕРШЕН ==============\n");
    return 0;
}
