#ifndef __RX__MACHINE__
#define __RX__MACHINE__

    typedef enum {
        STATE_WAIT_PILOT,   // Детектор Баркера выключен, ждем стабильный захват ФАПЧ
        STATE_SEARCH_BARKER, // Пилот захвачен, активируем скользящий Баркер до первой "иглы"
		STATE_RECEIVE_DATA
    } rx_state_t;

#endif
