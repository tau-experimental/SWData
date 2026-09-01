#include "mls_sync.h"
#include <string.h>
#include <math.h>

// Канонический массив знаков MLS-31
// Жесткий системный шаблон знаков М-последовательности MLS-31
// (Замените этот массив на ваши реальные 31 знаков из проекта, если они отличаются)
const int8_t mls_31_signs[MLS_LEN] = {
    //1, 1, 1, 1, 1, -1, -1, -1, 1, 1, -1, 1, -1, -1, 1, -1,
    //1, 1, -1, -1, -1, -1, 1, -1, 1, -1, 1, 1, 1, -1, -1
		1,  1,  1,  1,  1, -1, -1,  1,  1, -1,
		1, -1,  1,  1,  1, -1,  1, -1, -1, -1,
		1, -1, -1,  1, -1,  1, -1,  1, -1, -1, -1
};

/**
 * @brief Инициализация структуры синхронизатора.
 * Исправлен баг неявного приведения float->int8_t в шаблоне.
 */
void mls_init(mls_sync_t *sync) {
    // Полная очистка памяти структуры (все буферы в 0)
    memset(sync, 0, sizeof(mls_sync_t));

    // Сброс ищейки знака-пустышки
    sync->clk_smooth_min = 0.0f;
    sync->clk_min_hold_counter = 0;
    sync->clk_smooth_ptr = 0;
    sync->clk_smooth_sum = 0.0f;
    sync->prev_symbol_needle = 0.0f;

    // Генерация растянутого прямоугольного шаблона знаков MLS-31
    // Используем чистые целочисленные 1 и -1 для экономии памяти в int8_t
    for (int i = 0; i < MLS_LEN; i++) {
        int8_t current_sign = mls_31_signs[i];
        // Учитываем отрицательную пустышку перед началом последовательности
        int8_t prev_sign = (i == 0) ? -1 : mls_31_signs[i - 1];

        for (int j = 0; j < POINTS_PER_SYMBOL; j++) {
            int idx = i * POINTS_PER_SYMBOL + j;

            if (current_sign != prev_sign) {
                // Граница излома фазы (при прямоугольном профиле — жесткая полка)
                sync->template_mls_stretched[idx] = (current_sign > 0) ? 1 : -1;
            } else {
                // Фаза продолжает лежать на полке
                sync->template_mls_stretched[idx] = (current_sign > 0) ? 1 : -1;
            }
        }
    }

    sync->is_calibrated = 0;
    sync->spy = 0.0f;
}


#if 0
// рабочая, но не совсем точная функция
int mls_tick(mls_sync_t *sync, const cplx_f32 *input_sample, float *out_mls_needle, float *out_sc_power) {
    // По умолчанию игла равна нулю, пока не сработает окно децимации
    *out_mls_needle = 0.0f;
    static int dbg = 0;
    static clk_grid_state_t clk_state = CLK_WAIT_PILOT_STABLE;
    static float sync_symbol_max = 0.0f;
    static int max_hold_counter = 0;

    // =========================================================================
    // === СТУПЕНЬ 1: Вычисление скользящего Шмидля-Кокса (На каждом сэмпле) ===
    // =========================================================================
    int center_idx = sync->ptr + SC_HALF_LEN; // SC_HALF_LEN это 400
    if (center_idx >= SAMPLES_PER_SYMBOL) {
        center_idx -= SAMPLES_PER_SYMBOL;
    }

    cplx_f32 head   = *input_sample;
    cplx_f32 center = sync->delay_line[center_idx];
    cplx_f32 tail   = sync->delay_line[sync->ptr]; // Старая голова в этой ячейке — это хвост задержки 800

    // Шаг FIFO буфера
    sync->delay_line[sync->ptr] = head;

    // Дифференциальные произведения для CIC
    cplx_f32 d_curr; // Голова * conj(Центр)
    d_curr.re = head.re * center.re + head.im * center.im;
    d_curr.im = head.im * center.re - head.re * center.im;

    cplx_f32 d_old;  // Центр * conj(Хвост)
    d_old.re = center.re * tail.re + center.im * tail.im;
    d_old.im = center.im * tail.re - center.re * tail.im;

    // Обновление когерентного аккумулятора корреляции
    sync->running_sum.re += (d_curr.re - d_old.re);
    sync->running_sum.im += (d_curr.im - d_old.im);

    // Обновление энергии правого окна B (от Центра до Хвоста)
    float p_center = center.re * center.re + center.im * center.im;
    float p_tail   = tail.re * tail.re + tail.im * tail.im;
    sync->energy_b += (p_center - p_tail);

    // Продвигаем указатель кольцевого буфера сэмплов
    sync->ptr++;
    if (sync->ptr >= SAMPLES_PER_SYMBOL) {
        sync->ptr = 0;
    }

    // Расчет нормированной мощности первой ступени (чисто для вывода на график)
    if (sync->energy_b > 0.01f) {
        *out_sc_power = (sync->running_sum.re * sync->running_sum.re + sync->running_sum.im * sync->running_sum.im) / (sync->energy_b * sync->energy_b);
    } else {
        *out_sc_power = 0.0f;
    }

    // =========================================================================
	// === СТУПЕНЬ 2: Выпрямление фазы и децимированная «MLS-Ищейка» =======
	// =========================================================================

	// Мягкое решение (знак угла) по умолчанию равен нулю
	float soft_angle_decision = 0.0f;

	// Если калибровка по пилоту выполнена — включаем деротатор векторов
	if (sync->is_calibrated) {
		// Комплексное перемножение: r_clean = running_sum * conj(cal_vector)
		sync->derot.re = sync->running_sum.re * sync->calibre.re + sync->running_sum.im * sync->calibre.im;
		sync->derot.im = sync->running_sum.im * sync->calibre.re - sync->running_sum.re * sync->calibre.im;

		// ЖЕСТКАЯ ЗАЩИТА ОТ ДЕЛЕНИЯ НА НОЛЬ И ТИШИНЫ
		// Благодаря деротации, re_clean теперь всегда максимален (~160000 на чистом сигнале).
		// Если он падает ниже безопасного порога — мы в глубоком провале шума, обнуляем шаг.
		if (sync->derot.re > 100.0f) {
			// Вычисляем тангенс угла отклонения. Он идеально линеен на малых углах (до 45 градусов)
			// и в точности повторяет форму вашей оранжевой "пилы" фазы!
			soft_angle_decision = sync->derot.im / sync->derot.re;
		}
		if (dbg == 0) printf ("Calibre Re: %+.2f, Im: %+.2f\n", sync->calibre.re, sync->calibre.im);
		dbg = 1;
	} else {
		sync->derot.re = sync->running_sum.re;
		sync->derot.im = sync->running_sum.im;
	}

	switch (clk_state) {
	    case CLK_WAIT_PILOT_STABLE:
	        // В точке 8000, когда Костас ожил и калибровка выполнена:
	        if (sync->is_calibrated) {
	            clk_state = CLK_CATCH_SYNC_SYMBOL;
	            sync_symbol_max = 0.0f;
	            max_hold_counter = 0;
	        }
	        break;

	    case CLK_CATCH_SYNC_SYMBOL:
	        // 1. Скользящий CIC-интегратор сглаживания фазового трека
	        static int low_phase_duration_cnt = 0;
	        float outgoing_phase = sync->clk_smooth_buffer[sync->clk_smooth_ptr];
	        sync->clk_smooth_buffer[sync->clk_smooth_ptr] = soft_angle_decision;
	        sync->clk_smooth_ptr = (sync->clk_smooth_ptr + 1) % DECLK_WINDOW;
	        sync->clk_smooth_sum += (soft_angle_decision - outgoing_phase);

	        // ЛОГИКА ЗАЩИТЫ: Игнорируем всё, пока фаза не пошла на реальный штурм вниз!
	        // Пока soft_angle_decision болтается около нуля в шумах пилота, компаратор закрыт.
	        sync->spy = sync->clk_smooth_sum;

	        //if (soft_angle_decision < -0.20f) {
	        if (sync->clk_smooth_sum < -0.20f) {
	            low_phase_duration_cnt++;

	            // Включаем алгоритм поиска минимума площади ТОЛЬКО если фаза
	            // непрерывно находится в отрицательной зоне дольше 32 сэмплов (защита от шума!)
	            if (low_phase_duration_cnt > 32) {
	                if (sync->clk_smooth_sum < sync->clk_smooth_min) {
	                    sync->clk_smooth_min = sync->clk_smooth_sum;
	                    sync->clk_min_hold_counter = 0;
	                } else {
	                    // Зафиксировали прохождение дна холостого символа
	                    sync->clk_min_hold_counter++;
	                    if (sync->clk_min_hold_counter > 16) {
	                        // ТАКТОВЫЙ ЗАМОК СИНХРОНИЗИРОВАН!
	                        sync->decimation_counter = 100 + 16;
	                        sync->macro_ptr = 0;

	                        // Передаем шпиону b_step, чтобы на малиновом графике
	                        // четко увидеть координату сработки
	                        sync->spy = (float)-1000;

	                        clk_state = CLK_RUNNING_DECIMATOR;
	                        low_phase_duration_cnt = 0;
	                    }
	                }
	            }
	        } else {
	            // Если фаза выскочила обратно выше -0.2, сбрасываем счетчик длительности
	            low_phase_duration_cnt = 0;
	        }
	        break;

	    case CLK_RUNNING_DECIMATOR:

	    	// Каскад накопления децимации (раз в 100 сэмплов)
	    	sync->decimation_accumulator += soft_angle_decision;
	    	sync->decimation_counter++;

	    	if (sync->decimation_counter < DECIMATION_FACTOR) {
	    		return 0; // Продолжаем копить окно децимации
	    	}

	    	// Окно 100 сэмплов накопилось, усредняем трек фазы
	    	float averaged_phase = sync->decimation_accumulator / (float)DECIMATION_FACTOR;
	    	sync->decimation_accumulator = 0.0f;
	    	sync->decimation_counter = 0;

	    	// Заталкиваем выпрямленную фазовую точку в макро-историю узора
	    	sync->angle_trace_history[sync->macro_ptr] = averaged_phase;

	    	// Вычисляем дискретную свертку второй ступени по выпрямленному треку фазы
	    	float mls_corr_sum = 0.0f;
	    	float mls_energy = 0.0f;
	    	int current_idx = sync->macro_ptr;

	    	for (int i = (MLS_MACRO_HISTORY_LEN - 1); i >= 0; i--) {
	    		float val = sync->angle_trace_history[current_idx];
	    		int8_t sign = sync->template_mls_stretched[i];

	    		mls_corr_sum += val * (float)sign;
	    		mls_energy += val * val;

	    		current_idx--;
	    		if (current_idx < 0) {
	    			current_idx = MLS_MACRO_HISTORY_LEN - 1;
	    		}
	    	}

	    	// Продвигаем указатель макро-истории
	    	sync->macro_ptr++;
	    	if (sync->macro_ptr >= MLS_MACRO_HISTORY_LEN) {
	    		sync->macro_ptr = 0;
	    	}

	    	if (mls_energy < 0.001f) {
	    		return 0;
	    	}

	    	if (mls_energy > 0.01f) {
	    	    *out_mls_needle = (mls_corr_sum * mls_corr_sum) / (mls_energy * (float)MLS_MACRO_HISTORY_LEN);
	    	} else {
	    	    *out_mls_needle = 0.0f;
	    	}

	        break;
	}


	// Если игла пробила порог, преамбула захвачена с идеальным фазовым выравниванием!
	if (*out_mls_needle > 0.35f) {
		return 1;
	}

	return 0;
}
#else

int mls_tick(mls_sync_t *sync, const cplx_f32 *input_sample, float *out_mls_needle, float *out_sc_power) {
    *out_mls_needle = 0.0f;
    *out_sc_power = 0.0f;
    int is_needle_found = 0;

    static clk_grid_state_t clk_state = CLK_WAIT_PILOT_STABLE;
    static int low_phase_duration_cnt = 0;

    // =========================================================================
    // СТУПЕНЬ 1: Скользящее когерентное окно Шмидля-Кокса (800 сэмплов)
    // =========================================================================
    cplx_f32 old_sample = sync->delay_line[sync->ptr];
    int mid_idx = (sync->ptr + SC_HALF_LEN) % SAMPLES_PER_SYMBOL;
    cplx_f32 mid_sample = sync->delay_line[mid_idx];

    sync->delay_line[sync->ptr] = *input_sample;

    // Вычисляем через вашу библиотечную функцию умножения на сопряженное
    cplx_f32 term_new = cplx_mul_conj(mid_sample, *input_sample);
    cplx_f32 term_old = cplx_mul_conj(old_sample, mid_sample);

    sync->running_sum.re += term_new.re - term_old.re;
    sync->running_sum.im += term_new.im - term_old.im;

    float pwr_new = input_sample->re * input_sample->re + input_sample->im * input_sample->im;
    float pwr_old = mid_sample.re * mid_sample.re + mid_sample.im * mid_sample.im;
    sync->energy_b += pwr_new - pwr_old;

    float sc_power_num = sync->running_sum.re * sync->running_sum.re + sync->running_sum.im * sync->running_sum.im;
    float sc_denom = sync->energy_b * sync->energy_b;
    float current_sc_power = (sc_denom > 1e-6f) ? (sc_power_num / sc_denom) : 0.0f;

    sync->ptr = (sync->ptr + 1) % SAMPLES_PER_SYMBOL;

    // =========================================================================
    // СТУПЕНЬ 2: Вычисление фазы через деротатор (внешнее управление Костаса)
    // =========================================================================
    float soft_angle_decision = 0.0f;

    if (sync->is_calibrated) {
        // Программный доворот с правильной полярностью мнимой части
        sync->derot.re = sync->running_sum.re * sync->calibre.re + sync->running_sum.im * sync->calibre.im;
        sync->derot.im = sync->running_sum.re * sync->calibre.im - sync->running_sum.im * sync->calibre.re;

        if (sync->derot.re > 100.0f) {
            soft_angle_decision = sync->derot.im / sync->derot.re;
        }

        // Выводим трек фазы шпиону (теперь пустышка обязана смотреть вниз!)
        sync->spy = soft_angle_decision;
    } else {
        // Пока Костас не защелкнулся, выводим сырую мощность Шмидля-Кокса
        *out_sc_power = current_sc_power;
    }

    // =========================================================================
    // СТУПЕНЬ 3: Автомат тактовой сетки
    // =========================================================================
    switch (clk_state) {
        case CLK_WAIT_PILOT_STABLE:
            if (sync->is_calibrated) {
                clk_state = CLK_CATCH_SYNC_SYMBOL;
            }
            break;

        case CLK_CATCH_SYNC_SYMBOL:
            // Костас на связи, но пустышку еще ждем. Фиолетовая линия = -500
        	*out_sc_power = -500.0f;

			// Наш CIC-фильтр сглаживания площади знака-пустышки
			float outgoing_phase = sync->clk_smooth_buffer[sync->clk_smooth_ptr];
			sync->clk_smooth_sum += (soft_angle_decision - outgoing_phase);
			sync->clk_smooth_buffer[sync->clk_smooth_ptr] = soft_angle_decision;
			sync->clk_smooth_ptr = (sync->clk_smooth_ptr + 1) % DECLK_WINDOW;

			// === ПЕРЕХВАТ ШПИОНА ===
			// Выводим интегральную площадь площади вместо фазы!
			// Мы увидим плавную яму, по дну которой автомат ищет минимум.
			sync->spy = sync->clk_smooth_sum;

            // === ЖЕСТКИЙ БОЕВОЙ ПОРОГ ИНТЕГРАЛЬНОЙ ПЛОЩАДИ ===
            // Шум пилота никогда не пробьет -100.0f градусов.
            // Реальная пустышка гарантированно улетает до -170.
            if (sync->clk_smooth_sum < -100.0f) {
                low_phase_duration_cnt++;

                // Ждем стабильного удержания глубокого тренда
                if (low_phase_duration_cnt > 100) {
                    if (sync->clk_smooth_sum < sync->clk_smooth_min) {
                        sync->clk_smooth_min = sync->clk_smooth_sum;
                        sync->clk_min_hold_counter = 0;
                    } else {
                        sync->clk_min_hold_counter++;

                        // Окно фиксации прохождения дна
                        if (sync->clk_min_hold_counter > 40) {
                            // ЗАЩЕЛКА СИНХРОНИЗАЦИИ
                            sync->decimation_counter = 0;
                            sync->macro_ptr = 0;

                            clk_state = CLK_RUNNING_DECIMATOR;
                            low_phase_duration_cnt = 0;
                        }
                    }
                }
            } else {
                low_phase_duration_cnt = 0;
            }
			break;

        case CLK_RUNNING_DECIMATOR:
            *out_sc_power = -1000.0f; // Удерживаем статус работы ищейки
            sync->spy = 180*soft_angle_decision/3.1415; // Возвращаем фазу, чтобы видеть узор MLS


            // 1. Накапливаем фазу в оба интегратора параллельно
            sync->symbol_integrator_A += soft_angle_decision;

            // Канал Б активируется со сдвигом в 400 сэмплов относительно старта А
            if (sync->decimation_counter >= 400 || sync->channel_B_active) {
                sync->symbol_integrator_B += soft_angle_decision;
                sync->channel_B_active = 1; // Зафиксировали, что Б вошел в режим накопления
            }

            // Инкрементируем сквозной счетчик сэмплов внутри текущего символа
            sync->decimation_counter++;

            // Переменные для обсчета сверток
            float needle_A = 0.0f;
            float needle_B = 0.0f;

            // =========================================================================
            // ТОЧКА СРАБАТЫВАНИЯ КАНАЛА Б (Прошло ровно 400 сэмплов — середина символа А)
            // =========================================================================
            if (sync->decimation_counter == 400)
            {
                // Канал Б завершил накопление своего 800-сэмплового окна!
                float soft_symbol_B = sync->symbol_integrator_B / 800.0f;
                sync->symbol_integrator_B = 0.0f;

                sync->soft_mls_buffer_B[sync->symbol_ptr_B] = soft_symbol_B;

                // Считаем свертку для Канала Б
                float corr_sum_B = 0.0f;
                float energy_B = 0.0f;
                int idx_B = sync->symbol_ptr_B;

                for (int i = 0; i < MLS_LEN; i++) {
                    float val = sync->soft_mls_buffer_B[idx_B];
                    int8_t sign = mls_31_signs[MLS_LEN - 1 - i];
                    corr_sum_B += val * (float)sign;
                    energy_B += val * val;

                    idx_B--;
                    if (idx_B < 0) idx_B = MLS_LEN - 1;
                }

                if (energy_B > 0.01f) {
                    needle_B = (corr_sum_B * corr_sum_B) / (energy_B * (float)MLS_LEN);
                }

                // Продвигаем указатель Канала Б
                sync->symbol_ptr_B++;
                if (sync->symbol_ptr_B >= MLS_LEN) sync->symbol_ptr_B = 0;
            }

            // =========================================================================
            // ТОЧКА СРАБАТЫВАНИЯ КАНАЛА А (Прошло ровно 800 сэмплов — конец символа А)
            // =========================================================================
            if (sync->decimation_counter >= SAMPLES_PER_SYMBOL)
            {
                // Сбрасываем тактовый счетчик в ноль для следующего цикла
                sync->decimation_counter = 0;

                // Канал А завершил накопление своего 800-сэмплового окна
                float soft_symbol_A = sync->symbol_integrator_A / 800.0f;
                sync->symbol_integrator_A = 0.0f;

                sync->soft_mls_buffer_A[sync->symbol_ptr_A] = soft_symbol_A;

                // Считаем свертку для Канала А
                float corr_sum_A = 0.0f;
                float energy_A = 0.0f;
                int idx_A = sync->symbol_ptr_A;

                for (int i = 0; i < MLS_LEN; i++) {
                    float val = sync->soft_mls_buffer_A[idx_A];
                    int8_t sign = mls_31_signs[MLS_LEN - 1 - i];
                    corr_sum_A += val * (float)sign;
                    energy_A += val * val;

                    idx_A--;
                    if (idx_A < 0) idx_A = MLS_LEN - 1;
                }

                if (energy_A > 0.01f) {
                    needle_A = (corr_sum_A * corr_sum_A) / (energy_A * (float)MLS_LEN);
                }

                // Продвигаем указатель Канала А
                sync->symbol_ptr_A++;
                if (sync->symbol_ptr_A >= MLS_LEN) sync->symbol_ptr_A = 0;
            }

            // =========================================================================
            // ВЫБОР СИЛЬНЕЙШЕГО КАНАЛА И ВЗВОД ОБЩЕГО ТРИГГЕРА
            // =========================================================================
            // Отдаем наружу в main.c наибольшее значение иглы из двух каналов для графиков
            *out_mls_needle = (needle_A > needle_B) ? needle_A : needle_B;

            static int peak_latch = 0;

            if (!peak_latch) {
                // Если выстрелил Канал А (идеальная тактовая синхронизация первой ступени)
                if (needle_A > 0.5f) {
                    peak_latch = 1;
                    is_needle_found = 1; // Возвращаем 1 в main.c
                }
                // Если первая ступень промахнулась на полсимвола, выстрелит Канал Б!
                else if (needle_B > 0.5f) {
                    peak_latch = 2;      // Запоминаем, что победил ортогональный Канал Б
                    is_needle_found = 2; // Возвращаем двойку '2' как признак полусимвольного сдвига!
                }
            }
            break;

    }

    return is_needle_found;
}
#endif
