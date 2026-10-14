#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
extern atomic_int fake_open_calls, fake_close_calls, fake_write_calls, fake_flush_calls;
extern atomic_int fake_partial_next, fake_open_failure, fake_close_failure, fake_flush_failure;
extern atomic_int fake_task_failure, fake_health_stop_timeout, fake_event_tail_reads_after_delete;
extern atomic_int fake_watermark_after_exit, fake_nonzero_blocks;
extern atomic_int fake_nonzero_since_flush;
extern atomic_int fake_calibration_failure;
extern atomic_int fake_calibration_calls, fake_harmony_cut_calls, fake_harmony_cut_failure;
extern atomic_bool fake_harmony_cut_hold, fake_harmony_cut_blocked;
extern atomic_int fake_probe_calls, fake_probe_failure;
extern atomic_int fake_image_alloc_calls, fake_image_alloc_failure;
extern atomic_uint fake_image_alloc_caps;
extern atomic_int fake_measure_open_calls, fake_measure_read_calls, fake_measure_live;
extern atomic_int fake_measure_open_failure, fake_measure_read_failure, fake_measure_partial;
extern atomic_int fake_measure_expire;
extern atomic_int fake_analysis_calls, fake_analysis_blocked, fake_analysis_hold;
extern atomic_int fake_measure_gain;
extern atomic_int fake_measure_alloc_failure;
bool fake_measure_pcm_cleared(void);
void fake_set_health(int percent); /* -1 unknown, -2 absent, 0..100 validated */
void fake_hold_write(bool value);
void fake_hold_create(bool value);
void fake_hold_prepare(bool value);
bool fake_write_is_blocked(void);
bool fake_prepare_is_blocked(void);
bool fake_create_is_blocked(void);
void fake_sleep_ms(unsigned ms);
void fake_join_tasks(void);
