#ifndef __CHANNEL_RSSI_MONITOR__
#define __CHANNEL_RSSI_MONITOR__

#include "complex_math.h"

typedef struct {
    float delay_line[160];
    float running_sum;
    int   idx;
    int   ready;
    float noise_floor;
} channel_monitor_t;

void channel_monitor_init(channel_monitor_t *mon);
float channel_monitor_process(channel_monitor_t *mon, const cplx_f32 *raw_sample);

#endif
