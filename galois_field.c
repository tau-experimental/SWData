#include "galois_field.h"

/* Таблицы Галуа. На этапе моделирования заполняем динамически. */
/* При переносе на чип они превратятся в статические "const unsigned char ... [512] = { ... };" */
unsigned char gf_exp[512];
static unsigned char gf_log[256];

void gf_init(void) {
    int i;
    unsigned int x = 1;

    /* Используем стандартный полином x^8 + x^4 + x^3 + x^2 + 1 (0x11D) */
    unsigned int poly = 0x11D;

    for (i = 0; i < 255; i++) {
        gf_exp[i] = (unsigned char)x;
        gf_log[x] = (unsigned char)i;

        /* Умножение на x (сдвиг влево на 1 бит) */
        x <<= 1;

        /* Если вылетели за 8 бит (x >= 256), делаем XOR с полиномом */
        if (x & 0x100) {
            x ^= poly;
        }
    }

    /* Трюк с удвоением таблицы экспонент для предотвращения выхода за границы */
    for (i = 255; i < 512; i++) {
        gf_exp[i] = gf_exp[i - 255];
    }

    /* Особый случай для нуля: логарифм нуля не определен (в математике это минус бесконечность). */
    /* Нам это не помешает, так как в функциях умножения/деления мы обрабатываем нули отдельно. */
    gf_log[0] = 0;
}

unsigned char gf_add(unsigned char a, unsigned char b) {
    return a ^ b; /* Сложение в GF(256) — это всегда XOR */
}

unsigned char gf_sub(unsigned char a, unsigned char b) {
    return a ^ b; /* Вычитание в GF(256) — это то же самое, что сложение */
}

unsigned char gf_mul(unsigned char a, unsigned char b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    /* Благодаря таблице на 512 элементов, сложение gf_log[a] + gf_log[b] */
    /* никогда не вызовет падения программы, даже если сумма будет равна 510! */
    return gf_exp[gf_log[a] + gf_log[b]];
}

unsigned char gf_div(unsigned char a, unsigned char b) {
    int diff;
    if (a == 0) return 0;
    if (b == 0) return 0; /* Ошибка: деление на ноль! В реальном коде нужно избегать */

    /* Деление — это вычитание логарифмов. */
    /* Чтобы индекс не стал отрицательным, добавляем 255 (период таблицы) */
    diff = gf_log[a] - gf_log[b] + 255;
    return gf_exp[diff];
}
