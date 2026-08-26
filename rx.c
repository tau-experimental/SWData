#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#include "rx.h"

// Линейка из 4 независимых каналов
channel_filter_t rx_channels[NUM_DATA_TONES];

// Инициализация банка фильтров
void rx_filters_init(void) {
    for (int t = 0; t < NUM_DATA_TONES; t++) {
        float tone_freq = data_tones[t];
        rx_channels[t].freq_target = tone_freq;

        // Шаг фазы опорной частоты на один сэмпл
        float w0 = 2.0f * PI_F * tone_freq / FS;
        rx_channels[t].cos_w0 = cosf(w0);
        rx_channels[t].sin_w0 = sinf(w0);

        // Сброс состояний
        rx_channels[t].y_prev.re = 0.0f;
        rx_channels[t].y_prev.im = 0.0f;
        rx_channels[t].y_delayed.re = 0.0f;
        rx_channels[t].y_delayed.im = 0.0f;

        rx_channels[t].cfo_error_hz = 0.0f;
        rx_channels[t].phase_jump_metric = 0.0f;
        rx_channels[t].hold_counter = 0;
        rx_channels[t].is_switching = false;
    }
    printf("[RX_ENGINE] Высокодобротный банк фильтров (50 Гц) инициализирован.\n");
}

// Потоковая обработка одного комплексного сэмпла
// На входе: сырой сэмпл из эфира (или после гетеродина)
void rx_filters_process_sample(const complex_f *in_sample) {
    for (int t = 0; t < NUM_DATA_TONES; t++) {
        channel_filter_t *ch = &rx_channels[t];

        // 1. Комплексное гетеродинирование встроенного опорного сигнала "на лету"
        // Считаем X[n] * e^{-j * w0}
        float x_mixed_re = in_sample->re * ch->cos_w0 + in_sample->im * ch->sin_w0;
        float x_mixed_im = in_sample->im * ch->cos_w0 - in_sample->re * ch->sin_w0;

        // 2. Рекурсивное обновление скользящего Гёрцеля: Y[n] = ALPHA * Y[n-1] + Mixed
        complex_f y_curr;
        y_curr.re = ALPHA_POLE * ch->y_prev.re + x_mixed_re;
        y_curr.im = ALPHA_POLE * ch->y_prev.im + x_mixed_im;

        // 3. Вычисление автокорреляции: D[n] = Y[n] * Y*[n-1]
        complex_f d;
        d.re = y_curr.re * ch->y_prev.re + y_curr.im * ch->y_prev.im;
        d.im = y_curr.im * ch->y_prev.re - y_curr.re * ch->y_prev.im;

        // Проверяем наличие энергии в канале, чтобы избежать деления на ноль в тишине
        float magnitude_sq = y_curr.re * y_curr.re + y_curr.im * y_curr.im;
        if (magnitude_sq > 1e-5f) {
            // 4. Извлекаем угол разности фаз
            float delta_theta = atan2f(d.im, d.re);

            // Расчет CFO в Гц: отклонение частоты от опорной
            ch->cfo_error_hz = delta_theta * (FS / (2.0f * PI_F));

            // 5. Фазовый детектор: оценка стабильности частоты/фазы
            // Если идет стабильный тон (пусть и со сдвигом частоты), delta_theta константен.
            // Но в момент скачка фазы на границе символа, delta_theta резко срывается.
            // Метрика скачка: разница между текущим углом и предыдущим сохраненным трендом
            static float last_theta[NUM_DATA_TONES];
            float theta_diff = fabsf(delta_theta - last_theta[t]);
            if (theta_diff > PI_F) theta_diff = 2.0f * PI_F - theta_diff;

            ch->phase_jump_metric = theta_diff;
            last_theta[t] = delta_theta;

            // Пороговый детектор скачка фазы (экспериментальный порог, подлежит калибровке)
            if (ch->phase_jump_metric > 0.5f) {
                ch->is_switching = true;
                ch->hold_counter = HOLD_TIME_SAMPLES; // Взводим Hold-таймер удержания
            }
        } else {
            ch->cfo_error_hz = 0.0f;
            ch->phase_jump_metric = 0.0f;
        }

        // Логика работы оконного hold-таймера
        if (ch->hold_counter > 0) {
            ch->hold_counter--;
            if (ch->hold_counter == 0) {
                ch->is_switching = false; // Окно закрылось, сбрасываем подозрение
            }
        }

        // Сохраняем состояние для следующего сэмпла
        ch->y_prev = y_curr;
    }
}
