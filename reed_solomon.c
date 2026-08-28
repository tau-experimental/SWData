#include "reed_solomon.h"
#include "galois_field.h"
#include <string.h>

/* Инициализация геометрии и расчет генераторного полинома */
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
