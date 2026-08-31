#include "mls_sync.h"
#include <string.h>

// Канонический массив знаков MLS-31
static const int8_t mls_31_signs[31] = {
    1,  1,  1,  1,  1, -1, -1,  1,  1, -1,
    1, -1,  1,  1,  1, -1,  1, -1, -1, -1,
    1, -1, -1,  1, -1,  1, -1,  1, -1, -1, -1
};

void mls_init(mls_sync_t *sync) {
	memset (sync, 0, sizeof(mls_sync_t));
	// Сброс первой ступени
	sync->ptr = 0;
    sync->running_sum.re = 0.0f;
    sync->running_sum.im = 0.0f;
    sync->energy_b = 0.0f;
    memset(sync->delay_line, 0, sizeof(sync->delay_line));

    // Сброс второй ступени
    sync->macro_ptr = 0;
    sync->decimation_accumulator = 0.0f;
    sync->decimation_counter = 0;
    memset(sync->macro_vector_history, 0, sizeof(sync->macro_vector_history));
    memset(sync->angle_trace_history, 0, sizeof(sync->angle_trace_history));

#if 0
    // Заполнение растянутого шаблона: каждый знак MLS повторяется 8 раз подряд
    for (int i = 0; i < MLS_LEN; i++) {
        for (int j = 0; j < POINTS_PER_SYMBOL; j++) {
            int idx = i * POINTS_PER_SYMBOL + j;
            sync->template_mls_stretched[idx] = mls_31_signs[i];
        }
    }
#else
    // Шаблон со скошенными фронтами для одного символа из 8 точек децимации:
    // Вместо [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]
    // Мы делаем плавный набег фазы:
    //const float triangle_profile[8] = {0.25f, 0.5f, 0.75f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    const float triangle_profile[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}; // прямоугольный профиль шаблона
    for (int i = 0; i < MLS_LEN; i++) {
        int8_t current_sign = mls_31_signs[i];
        int8_t prev_sign = (i == 0) ? -1 : mls_31_signs[i - 1]; // учитываем нашу отрицательную пустышку!

        for (int j = 0; j < POINTS_PER_SYMBOL; j++) {
            int idx = i * POINTS_PER_SYMBOL + j;

            if (current_sign != prev_sign) {
                // Если на этом символе произошел излом фазы, используем плавный профиль
                if (current_sign > 0) {
                    sync->template_mls_stretched[idx] = triangle_profile[j];
                } else {
                    sync->template_mls_stretched[idx] = -triangle_profile[j];
                }
            } else {
                // Если фаза продолжает лежать на полке (знак не изменился)
                sync->template_mls_stretched[idx] = (current_sign > 0) ? 1.0f : -1.0f;
            }
        }
    }
#endif
    sync->is_calibrated = 0;
}

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
#if 0
	        // Холостой символ пошел вверх. Ищем абсолютный максимум на каждом сэмпле.
	        // Берем порог с запасом (например, > 0.15 в радианах), чтобы отсечь шум
	        if (soft_angle_decision > 0.15f) {
	            if (soft_angle_decision > sync_symbol_max) {
	                sync_symbol_max = soft_angle_decision;
	                max_hold_counter = 0; // Сбрасываем счетчик удержания пика
	            } else {
	                // Если фаза перестала расти и начала падать (прошли вершину),
	                // или держится на плато. Ждем стабильного спада, например, 5-10 сэмплов,
	                // чтобы защититься от мелкого шумового джиттера на макушке.
	                max_hold_counter++;
	                if (max_hold_counter > 8) {
	                    // ВЕРШИНА ПОЙМАНА! Физическая граница символов найдена.
	                    // Накатываем коррекцию назад на величину удержания пика (8 сэмплов)
	                    // и принудительно защелкиваем децимирующий счетчик в идеальный ноль!
	                    sync->decimation_counter = 8;
	                    sync->macro_ptr = 0; // Сбрасываем макро-историю

	                    clk_state = CLK_RUNNING_DECIMATOR; // Запускаем ищейку MLS
	                }
	            }
	        }
#else
	        // 1. Скользящий CIC-интегратор сглаживания фазового трека
	        static int low_phase_duration_cnt = 0;
	        float outgoing_phase = sync->clk_smooth_buffer[sync->clk_smooth_ptr];
	        sync->clk_smooth_sum += (soft_angle_decision - outgoing_phase);

	        // ЛОГИКА ЗАЩИТЫ: Игнорируем всё, пока фаза не пошла на реальный штурм вниз!
	        // Пока soft_angle_decision болтается около нуля в шумах пилота, компаратор закрыт.
	        sync->spy = sync->clk_smooth_sum;

#if 0
	        if (soft_angle_decision < -0.30f) {
	            // Включаем пиковый детектор площади только внутри истинного треугольника пустышки
	            if (sync->clk_smooth_sum < sync->clk_smooth_min) {
	                sync->clk_smooth_min = sync->clk_smooth_sum;
	                sync->clk_min_hold_counter = 0;
	            } else {
	                // Фаза пошла вверх к нулю — мы прошли реальное дно холостого символа!
	                sync->clk_min_hold_counter++;

	                if (sync->clk_min_hold_counter > 16) {
	                    // ТАКТОВАЯ СЕТКА ЗАЩЕЛКНУТА СВЕРХНАДЕЖНО!
	                    sync->decimation_counter = 100 + 16;
	                    sync->macro_ptr = 0;

	                    // Записываем маркер отладки для шпиона:
	                    // пускай малиновая линия станет равна 1.0 строго в момент ИСТИННОЙ сработки
	                    sync->spy = 1.0f;

	                    clk_state = CLK_RUNNING_DECIMATOR;
	                    //printf("[CLK] Истинный тактовый замок защелкнут на отсчете %d\n", b_step);
	                }
	            }
	        }
#else
	        if (soft_angle_decision < -0.20f) {
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
#endif
#endif
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

	    	//sync->spy = *out_mls_needle;
	    	/*static float needle_max = 0;

	    	if (sync->spy > 0.5) {
	    		if (sync->spy > needle_max) needle_max = sync->spy;
	    		if (sync->spy < needle_max) return 1; // тупейший детектор максимума
	    	}*/
	        break;
	}


	// Если игла пробила порог, преамбула захвачена с идеальным фазовым выравниванием!
	if (*out_mls_needle > 0.35f) {
		return 1;
	}

	return 0;
}
