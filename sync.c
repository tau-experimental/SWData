#include "sync.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void dsp_sync_init(clock_recovery_t *sync, uint32_t symbol_len) {
    sync->state = SYNC_STATE_SEARCH;
    sync->expected_symbol_len = symbol_len;
    sync->allowed_jitter = symbol_len / 10; // Расширим ворота до +-80 сэмплов под диким шумом!

    sync->last_peak_time = 0;
    sync->first_interval = 0;
    sync->s_prev = 0.0f;
    sync->s_pprev = 0.0f;
    sync->ascending = false;
    sync->sample_counter = 0;
    sync->symbol_timer = 0;
    sync->blanking_timer = 0;
    sync->noise_floor = 5.0f;
    sync->peak_count = 0;
}

bool dsp_sync_step(clock_recovery_t *sync, float s_curr) {
    sync->sample_counter++;
    bool strobe = false;

    // АРУ (надежный Bottom Envelope Tracker)
    if (s_curr < sync->noise_floor) {
        sync->noise_floor = 0.05f * s_curr + 0.95f * sync->noise_floor;
    } else {
        if (sync->noise_floor < s_curr * 0.5f) {
            sync->noise_floor = 0.01f * s_curr + 0.99f * sync->noise_floor;
        } else {
            sync->noise_floor = 0.0001f * s_curr + 0.9999f * sync->noise_floor;
        }
    }
    float dynamic_threshold = sync->noise_floor * 1.35f;
    if (dynamic_threshold < 12.0f) dynamic_threshold = 12.0f;

    if (sync->blanking_timer > 0) sync->blanking_timer--;

    static int fall_counter = 0;
    static float peak_candidate_val = 0.0f;
    static uint32_t peak_candidate_time = 0;

    // Детектор пиков работает всегда
    if (s_curr > sync->s_prev) {
        if (s_curr > dynamic_threshold && sync->blanking_timer == 0) {
            if (s_curr > peak_candidate_val) {
                peak_candidate_val = s_curr;
                peak_candidate_time = sync->sample_counter;
            }
            fall_counter = 0;
        }
    }
    else if (s_curr < sync->s_prev && peak_candidate_time > 0) {
        fall_counter++;

        if (fall_counter == 5) {
            uint32_t current_peak_time = peak_candidate_time;
            uint32_t interval = current_peak_time - sync->last_peak_time;
            sync->blanking_timer = 600; // Защита от дребезга макушки

            switch (sync->state) {
                case SYNC_STATE_SEARCH:
                    sync->last_peak_time = current_peak_time;
                    sync->state = SYNC_STATE_CHECK_1;
                    break;

                case SYNC_STATE_CHECK_1:
                    if (abs((int)interval - (int)sync->expected_symbol_len) <= (int)sync->allowed_jitter) {
                        sync->first_interval = interval;
                        sync->last_peak_time = current_peak_time;
                        sync->state = SYNC_STATE_LOCKED;
                        sync->symbol_timer = 5;
                        strobe = true; // Первый строб по факту LOCK
                        printf("[SYNC] !!! LOCK !!! Адаптивный шаг найден: %d сэмплов.\n", interval);
                        sync->peak_count = 0;
                    } else {
                        sync->last_peak_time = current_peak_time;
                    }
                    break;

                case SYNC_STATE_LOCKED:
                    {
                        // ПЛАВНАЯ ПОДСТРОЙКА DPLL ПО РЕАЛЬНЫМ ПИКАМ ЭФИРА
                        int expected_timer_val = 5;
                        int timing_error = (int)sync->symbol_timer - expected_timer_val;

                        if (timing_error > (int)sync->expected_symbol_len / 2) timing_error -= sync->expected_symbol_len;
                        if (timing_error < -(int)sync->expected_symbol_len / 2) timing_error += sync->expected_symbol_len;

                        // Мягко корректируем таймер на 15% от ошибки джиттера
                        float dpll_gain = 0.15f;
                        int adjustment = (int)(timing_error * dpll_gain);

                        sync->symbol_timer -= adjustment;
                        sync->last_peak_time = current_peak_time;
                    }
                    break;
            }

            peak_candidate_time = 0;
            peak_candidate_val = 0.0f;
            fall_counter = 0;
        }
    }

    // В режиме LOCK строб выдается математическим таймером, который непрерывно подправляется DPLL!
    if (sync->state == SYNC_STATE_LOCKED) {
        sync->symbol_timer++;
        if (sync->symbol_timer >= sync->expected_symbol_len) {
            strobe = true; // Строб летит в декодер строго по скорректированному расписанию!
            sync->peak_count++;
            //printf("[SYNC] Пик #%d обнаружен. Выдаем строб!\n", sync->peak_count, sync->symbol_timer);
            sync->symbol_timer = 0;
        }
    }

    sync->s_prev = s_curr;
    return strobe;
}
