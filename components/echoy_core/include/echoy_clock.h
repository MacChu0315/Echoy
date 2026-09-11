// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_CLOCK_H
#define ECHOY_CLOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ECHOY_CLOCK_SAMPLES 96
#define ECHOY_CLOCK_WINDOW_US INT64_C(20000000)
#define ECHOY_CLOCK_MAX_RTT_US INT64_C(100000)
#define ECHOY_CLOCK_MAX_UNCERTAINTY_US 2000.0
typedef struct { double at_us, offset_us, rtt_us; } echoy_clock_sample_t;
typedef struct {
    echoy_clock_sample_t samples[ECHOY_CLOCK_SAMPLES]; size_t count;
    int64_t last_t1, last_t4; double anchor_us, offset_us, rate, uncertainty_us;
    bool stable; unsigned rejected;
} echoy_clock_t;
void echoy_clock_init(echoy_clock_t *c);
bool echoy_clock_observe(echoy_clock_t *c, int64_t t1, int64_t t2, int64_t t3, int64_t t4);
bool echoy_clock_remote_to_local(const echoy_clock_t *c, int64_t remote, int64_t *local);
#endif
