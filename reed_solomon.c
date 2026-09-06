#include "reed_solomon.h"
#include "galois_field.h"
#include <string.h>

const uint8_t gen_poly_rs_universal[7] = { 0x75, 0x31, 0x3A, 0x9E, 0x04, 0x7E, 0x01 };

const rs_config_t rs_config_ack_14_8 = {
    .n = 14,
    .k = 8,
    .t2 = 6,
    .gen_poly = gen_poly_rs_universal
};

const rs_config_t rs_config_data_26_20 = {
    .n = 26,
    .k = 20,
    .t2 = 6,
    .gen_poly = gen_poly_rs_universal
};

/* Инициализация геометрии и расчет генераторного полинома */
#if 0
void rs_init_geometry(rs_config_t *cfg, int n, int k) {
    int i, j;
    cfg->n = n;
    cfg->k = k;
    cfg->t2 = n - k;

    /* Инициализируем полином: gen_poly[0] = 1 */
    memset(cfg->gen_poly, 0, sizeof(cfg->gen_poly));
    cfg->gen_poly[0] = 1;

    /* Классическое построение: g(x) = (x + g^1)*(x + g^2)*...*(x + g^t2) */
    for (i = 1; i <= cfg->t2; i++) {
        unsigned char root = gf_exp[i]; /* Корни — последовательные степени g */

        /* Сдвигаем и умножаем полином на (x + root) */
        cfg->gen_poly[i] = cfg->gen_poly[i - 1];
        for (j = i - 1; j > 0; j--) {
            cfg->gen_poly[j] = gf_add(cfg->gen_poly[j - 1], gf_mul(cfg->gen_poly[j], root));
        }
        cfg->gen_poly[0] = gf_mul(cfg->gen_poly[0], root);
    }
}
#endif

/* Кодирование: Честное деление многочленов в столбик */
void rs_encode_flexible(const rs_config_t *cfg, const unsigned char *msg_in, unsigned char *parity_out) {
    int i, j;
    unsigned char feedback;

    memset(parity_out, 0, cfg->t2);

    /* Обработка информационных байт */
    for (i = 0; i < cfg->k; i++) {
        feedback = gf_add(msg_in[i], parity_out[0]);

        for (j = 0; j < cfg->t2 - 1; j++) {
            // Было: parity_out[j] = gf_add(parity_out[j + 1], gf_mul(feedback, cfg->gen_poly[cfg->t2 - j]));
            parity_out[j] = gf_add(parity_out[j + 1], gf_mul(feedback, cfg->gen_poly[cfg->t2 - 1 - j]));
        }
        // Было: parity_out[cfg->t2 - 1] = gf_mul(feedback, cfg->gen_poly[1]);
        parity_out[cfg->t2 - 1] = gf_mul(feedback, cfg->gen_poly[0]);
    }
}

void rs_encode_fast(const rs_config_t *cfg, const unsigned char *msg_in, unsigned char *parity_out) {
    int i, j;
    unsigned char feedback;

    // Очищаем буфер защиты перед расчетом (cfg->t2 байт)
    memset(parity_out, 0, cfg->t2);

    /* Обработка каждого информационного байта пакета */
    for (i = 0; i < cfg->k; i++) {
        // Обратная связь: складываем текущий байт данных со старшим байтом текущей защиты
        // В поле Галуа gf_add — это обычный XOR, выполняется за 1 такт RISC-V
        feedback = gf_add(msg_in[i], parity_out[0]);

        if (feedback != 0) {
            // ОПТИМИЗАЦИЯ ПОД RISC-V: если feedback == 0, умножение gf_mul всегда вернет 0.
            // Пропуск цикла при нулевом feedback экономит до 30-40% тактов процессора на реальных пакетах!

            for (j = 0; j < cfg->t2 - 1; j++) {
                // Прямой обход полинома: коэффициент gen_poly[j] должен соответствовать
                // вкладу обратной связи в j-й элемент регистра защиты.
                parity_out[j] = gf_add(parity_out[j + 1], gf_mul(feedback, cfg->gen_poly[cfg->t2 - 1 - j]));
            }
            // Последний элемент регистра защиты зависит от младшего коэффициента полинома
            parity_out[cfg->t2 - 1] = gf_mul(feedback, cfg->gen_poly[0]);
        } else {
            // Если обратная связь нулевая, просто сдвигаем регистр защиты влево
            for (j = 0; j < cfg->t2 - 1; j++) {
                parity_out[j] = parity_out[j + 1];
            }
            parity_out[cfg->t2 - 1] = 0;
        }
    }
}

/* Декодирование пакета */
int rs_decode_flexible(const rs_config_t *cfg, unsigned char *packet_inout) {
    int i, j, r;
    int t = cfg->t2 / 2;

    unsigned char syn[RS_MAX_2T];
    int syn_error = 0;

    /* --------------------------------------------------------------------- */
    /* СТАДИЯ 1: Вычисление Синдромов (Академическая схема Горнера)        */
    /* --------------------------------------------------------------------- */
    for (i = 0; i < cfg->t2; i++) {
        unsigned char s = 0;
        unsigned char root = gf_exp[i + 1]; /* Подставляем корни g^1, g^2... */
        for (j = 0; j < cfg->n; j++) {
            s = gf_add(packet_inout[j], gf_mul(s, root));
        }
        syn[i] = s;
        if (s != 0) syn_error = 1;
    }

    if (!syn_error) return 0; /* Ошибок нет, чистый канал! */

    /* --------------------------------------------------------------------- */
    /* СТАДИЯ 2: Алгоритм Берлекэмпа-Месси                                    */
    /* --------------------------------------------------------------------- */
    unsigned char lambda[RS_MAX_2T + 1];
    unsigned char b[RS_MAX_2T + 1];
    unsigned char t_poly[RS_MAX_2T + 1];

    memset(lambda, 0, sizeof(lambda));
    memset(b, 0, sizeof(b));
    lambda[0] = 1;
    b[0] = 1;

    int l = 0;
    int m = 1;

    for (r = 0; r < cfg->t2; r++) {
        unsigned char d = syn[r];
        for (i = 1; i <= l; i++) {
            d = gf_add(d, gf_mul(lambda[i], syn[r - i]));
        }

        if (d == 0) {
            m++;
        } else {
            memcpy(t_poly, lambda, sizeof(lambda));

            for (i = 0; i <= cfg->t2 - m; i++) {
                lambda[i + m] = gf_add(lambda[i + m], gf_mul(d, b[i]));
            }

            if (2 * l <= r) {
                l = r + 1 - l;
                for (i = 0; i <= cfg->t2; i++) {
                    b[i] = gf_div(t_poly[i], d);
                }
                m = 1;
            } else {
                m++;
            }
        }
    }

    if (l > t) return -1; /* Ошибок больше, чем t = 3 -> Отказ */

    /* --------------------------------------------------------------------- */
    /* СТАДИЯ 3: Поиск Чэня (Сканирование позиций)                           */
    /* --------------------------------------------------------------------- */
    int err_pos[RS_MAX_2T];
    int err_count = 0;

    for (i = 0; i < cfg->n; i++) {
        /* В классическом коде CCSDS положение символа x^pos определяется */
        /* как расстояние от конца пакета: pos = n - 1 - i */
        unsigned char x_inv = gf_exp[255 - (cfg->n - 1 - i)];
        unsigned char sum = 1;
        unsigned char x_inv_pow = x_inv;

        for (j = 1; j <= l; j++) {
            sum = gf_add(sum, gf_mul(lambda[j], x_inv_pow));
            x_inv_pow = gf_mul(x_inv_pow, x_inv);
        }

        if (sum == 0) {
            err_pos[err_count] = i; /* Запоминаем физический индекс ошибки */
            err_count++;
        }
    }

    if (err_count != l) return -1; /* Число корней не совпало со степенью лямбды */

    /* --------------------------------------------------------------------- */
    /* СТАДИЯ 4: Алгоритм Форни (Расчет масок без операции %)                */
    /* --------------------------------------------------------------------- */
    unsigned char omega[RS_MAX_2T];
    memset(omega, 0, sizeof(omega));
    for (i = 0; i < cfg->t2; i++) {
        unsigned char sum = syn[i];
        for (j = 1; j <= i; j++) {
            sum = gf_add(sum, gf_mul(lambda[j], syn[i - j]));
        }
        omega[i] = sum;
    }

    for (i = 0; i < err_count; i++) {
        int pos = err_pos[i];
        // unsigned char x_val = gf_exp[cfg->n - 1 - pos]; // после удаления num = gf_mul(num, x_val); стало не нужно
        unsigned char x_inv = gf_exp[255 - (cfg->n - 1 - pos)];

        /* Вычисление числителя полинома ошибок */
        unsigned char num = 0;
        unsigned char x_inv_pow = 1;
        for (j = 0; j < cfg->t2; j++) {
            num = gf_add(num, gf_mul(omega[j], x_inv_pow));
            x_inv_pow = gf_mul(x_inv_pow, x_inv);
        }
        //Было: num = gf_mul(num, x_val); > Удалено

        /* Вычисление знаменателя (производной) */
        unsigned char den = 0;
        x_inv_pow = 1;
        for (j = 1; j <= l; j++) {
            if (j & 1) {
                den = gf_add(den, gf_mul(lambda[j], x_inv_pow));
            }
            x_inv_pow = gf_mul(x_inv_pow, x_inv);
        }

        if (den == 0) return -1;

        /* Наложение маски исправления ошибки */
        unsigned char error_mask = gf_div(num, den);
        packet_inout[pos] = gf_add(packet_inout[pos], error_mask);
    }

    return err_count;
}

int rs_decode_fast(const rs_config_t *cfg, unsigned char *packet_inout) {
#if 0
    int i, j, r;
    int t = cfg->t2 / 2;

    unsigned char syn[RS_MAX_2T];
    int syn_error = 0;

    /* --------------------------------------------------------------------- */
    /* СТАДИЯ 1: Вычисление Синдромов (Схема Горнера)                        */
    /* --------------------------------------------------------------------- */
    for (i = 0; i < cfg->t2; i++) {
        unsigned char s = 0;
        unsigned char root = gf_exp[i + 1]; /* Корни g^1, g^2... */
        for (j = 0; j < cfg->n; j++) {
            s = gf_add(packet_inout[j], gf_mul(s, root));
        }
        syn[i] = s;
        if (s != 0) syn_error = 1;
    }

    if (!syn_error) return 0; /* Ошибок нет, чистый канал! */

    /* --------------------------------------------------------------------- */
    /* СТАДИЯ 2: Алгоритм Берлекэмпа-Месси (Оптимизирован под RISC-V)       */
    /* --------------------------------------------------------------------- */
    unsigned char lambda[RS_MAX_2T + 1];
    unsigned char b[RS_MAX_2T + 1];
    unsigned char t_poly[RS_MAX_2T + 1];

    memset(lambda, 0, sizeof(lambda));
    memset(b, 0, sizeof(b));
    lambda[0] = 1;
    b[0] = 1;

    int l = 0;
    int m = 1;

    for (r = 0; r < cfg->t2; r++) {
        unsigned char d = syn[r];
        for (i = 1; i <= l; i++) {
            d = gf_add(d, gf_mul(lambda[i], syn[r - i]));
        }

        if (d == 0) {
            m++;
        } else {
            memcpy(t_poly, lambda, sizeof(lambda));

            for (i = 0; i <= cfg->t2 - m; i++) {
                lambda[i + m] = gf_add(lambda[i + m], gf_mul(d, b[i]));
            }

            if (2 * l <= r) {
                l = r + 1 - l;

                // РАЗГРУЗКА RISC-V: Вычисляем деление ОДИН раз вместо цикла!
                unsigned char d_inv = gf_div(1, d);
                for (i = 0; i <= cfg->t2; i++) {
                    b[i] = gf_mul(t_poly[i], d_inv); // Замена gf_div на gf_mul
                }
                m = 1;
            } else {
                m++;
            }
        }
    }

    if (l > t) return -1; /* Ошибок больше, чем t -> Неисправимый крах пакета */

    /* --------------------------------------------------------------------- */
    /* СТАДИЯ 3: Поиск Чэня (Сканирование позиций укороченного кода)         */
    /* --------------------------------------------------------------------- */
    int err_pos[RS_MAX_2T];
    int err_count = 0;

    for (i = 0; i < cfg->n; i++) {
        // КОРРЕКЦИЯ ДЛЯ SHORTENED CODE: укороченный пакет привязан к концу поля GF(2^8)
        //unsigned char x_inv = gf_exp[255 - (255 - cfg->n + i)];
    	unsigned char x_inv = gf_exp[255 - (cfg->n - 1 - i)]; // Для стадии 3 (индекс i)
        unsigned char sum = 1;
        unsigned char x_inv_pow = x_inv;

        for (j = 1; j <= l; j++) {
            sum = gf_add(sum, gf_mul(lambda[j], x_inv_pow));
            x_inv_pow = gf_mul(x_inv_pow, x_inv);
        }

        if (sum == 0) {
            err_pos[err_count] = i;
            err_count++;
        }
    }

    if (err_count != l) return -1; /* Число найденных корней не совпало со степенью лямбды */

    /* --------------------------------------------------------------------- */
    /* СТАДИЯ 4: Алгоритм Форни (Расчет масок и исправление)                */
    /* --------------------------------------------------------------------- */
    unsigned char omega[RS_MAX_2T];
    memset(omega, 0, sizeof(omega));
    for (i = 0; i < cfg->t2; i++) {
        unsigned char sum = syn[i];
        for (j = 1; j <= i; j++) {
            sum = gf_add(sum, gf_mul(lambda[j], syn[i - j]));
        }
        omega[i] = sum;
    }

    for (i = 0; i < err_count; i++) {
        int pos = err_pos[i];
        //unsigned char x_inv = gf_exp[255 - (255 - cfg->n + pos)];
        unsigned char x_inv = gf_exp[255 - (cfg->n - 1 - pos)]; // Для стадии 4 (индекс pos)

        /* Вычисление числителя полинома ошибок */
        unsigned char num = 0;
        unsigned char x_inv_pow = 1;
        for (j = 0; j < cfg->t2; j++) {
            num = gf_add(num, gf_mul(omega[j], x_inv_pow));
            x_inv_pow = gf_mul(x_inv_pow, x_inv);
        }

        /* Исправленное вычисление знаменателя (производной Форни) */
        unsigned char den = 0;
        x_inv_pow = 1; // Соответствует x_inv^0 = 1 для j=1 (\lambda_1)
        for (j = 1; j <= l; j++) {
            if (j & 1) {
                den = gf_add(den, gf_mul(lambda[j], x_inv_pow));
            }
            x_inv_pow = gf_mul(x_inv_pow, x_inv);
        }

        if (den == 0) return -1;

        /* Наложение маски исправления ошибки */
        unsigned char error_mask = gf_div(num, den);
        packet_inout[pos] = gf_add(packet_inout[pos], error_mask);
    }

    return err_count;
#else
	int i, j, r;
	int t = cfg->t2 / 2;

	unsigned char syn[RS_MAX_2T];
	int syn_error = 0;

	/* СТАДИЯ 1: Синдромы */
	for (i = 0; i < cfg->t2; i++) {
		unsigned char s = 0;
		unsigned char root = gf_exp[i + 1];
		for (j = 0; j < cfg->n; j++) {
			s = gf_add(packet_inout[j], gf_mul(s, root));
		}
		syn[i] = s;
		if (s != 0) syn_error = 1;
	}

	if (!syn_error) return 0; /* Чистый пакет */

	/* СТАДИЯ 2: Берлекэмп-Месси (Оптимизирован под RISC-V без % и лишних делений) */
	unsigned char lambda[RS_MAX_2T + 1];
	unsigned char b[RS_MAX_2T + 1];
	unsigned char t_poly[RS_MAX_2T + 1];

	memset(lambda, 0, sizeof(lambda));
	memset(b, 0, sizeof(b));
	lambda[0] = 1;
	b[0] = 1;

	int l = 0;
	int m = 1;

	for (r = 0; r < cfg->t2; r++) {
		unsigned char d = syn[r];
		for (i = 1; i <= l; i++) {
			d = gf_add(d, gf_mul(lambda[i], syn[r - i]));
		}

		if (d == 0) {
			m++;
		} else {
			memcpy(t_poly, lambda, sizeof(lambda));

			for (i = 0; i <= cfg->t2 - m; i++) {
				lambda[i + m] = gf_add(lambda[i + m], gf_mul(d, b[i]));
			}

			if (2 * l <= r) {
				l = r + 1 - l;

				// ВАЖНАЯ ОПТИМИЗАЦИЯ ДЛЯ МК: делим 1 раз вместо цикла!
				unsigned char d_inv = gf_div(1, d);
				for (i = 0; i <= cfg->t2; i++) {
					b[i] = gf_mul(t_poly[i], d_inv); // Быстрое умножение
				}
				m = 1;
			} else {
				m++;
			}
		}
	}

	if (l > t) return -1;

	/* СТАДИЯ 3: Поиск Чэня (ВАШИ ИСХОДНЫЕ ИНДЕКСЫ ПОЛЯ) */
	int err_pos[RS_MAX_2T];
	int err_count = 0;

	for (i = 0; i < cfg->n; i++) {
		unsigned char x_inv = gf_exp[255 - (cfg->n - 1 - i)]; // Ваша проверенная строка
		unsigned char sum = 1;
		unsigned char x_inv_pow = x_inv;

		for (j = 1; j <= l; j++) {
			sum = gf_add(sum, gf_mul(lambda[j], x_inv_pow));
			x_inv_pow = gf_mul(x_inv_pow, x_inv);
		}

		if (sum == 0) {
			err_pos[err_count] = i;
			err_count++;
		}
	}

	if (err_count != l) return -1;

	/* СТАДИЯ 4: Алгоритм Форни (ВАШ ПРОВЕРЕННЫЙ РАСЧЕТ ПРОИЗВОДНОЙ) */
	unsigned char omega[RS_MAX_2T];
	memset(omega, 0, sizeof(omega));
	for (i = 0; i < cfg->t2; i++) {
		unsigned char sum = syn[i];
		for (j = 1; j <= i; j++) {
			sum = gf_add(sum, gf_mul(lambda[j], syn[i - j]));
		}
		omega[i] = sum;
	}

	for (i = 0; i < err_count; i++) {
		int pos = err_pos[i];
		unsigned char x_inv = gf_exp[255 - (cfg->n - 1 - pos)]; // Ваша проверенная строка

		/* Вычисление числителя */
		unsigned char num = 0;
		unsigned char x_inv_pow = 1;
		for (j = 0; j < cfg->t2; j++) {
			num = gf_add(num, gf_mul(omega[j], x_inv_pow));
			x_inv_pow = gf_mul(x_inv_pow, x_inv);
		}

		/* Вычисление знаменателя (Ваш исходный цикл) */
		unsigned char den = 0;
		x_inv_pow = 1;
		for (j = 1; j <= l; j++) {
			if (j & 1) {
				den = gf_add(den, gf_mul(lambda[j], x_inv_pow));
			}
			x_inv_pow = gf_mul(x_inv_pow, x_inv);
		}

		if (den == 0) return -1;

		/* Наложение маски */
		unsigned char error_mask = gf_div(num, den);
		packet_inout[pos] = gf_add(packet_inout[pos], error_mask);
	}

	return err_count;
#endif
}

