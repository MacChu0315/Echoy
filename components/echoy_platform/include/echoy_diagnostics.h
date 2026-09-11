// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "echoy_control.h"
#include "echoy_audio.h"
typedef struct {
    char iris_id[33];uint64_t iris_boot;unsigned radio_dropped,contact_errors,heap_free,stack_free,psram_free,ui_updates,touch_processed,touch_dropped;
    int ui_error,audio_error,settings_error,battery_error,imu_error,module_error[2],module_type[2],module_state[2];
    unsigned battery_failures,settings_failures,audio_queue_failures;uint64_t output_gaps,plan_rejections;int64_t max_render_write_us;bool injection_enabled;
    echoy_audio_stats_t audio;
} echoy_diagnostics_t;
/* Bounded JSON, no pairing API or credential-bearing input. */
bool echoy_diagnostics_format(const echoy_control_t *,const echoy_diagnostics_t *,char *,size_t,size_t *);
bool echoy_diagnostics_injection(const uint8_t *,size_t,bool enabled,echoy_ui_event_t *);
bool echoy_diagnostics_touch(const uint8_t *,size_t,bool enabled,echoy_pointer_t *,int *x,int *y);
