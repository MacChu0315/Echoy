#pragma once
#include "echoy_arrange.h"
#include "echoy_audio.h"
#include "echoy_control.h"
typedef struct echoy_band echoy_band_t;
typedef struct echoy_band_job echoy_band_job_t;
typedef struct {
    uint32_t clock_cycles, voice_cycles, cue_cycles, tick_calls;
} echoy_band_profile_t;
/* First rendered block, measured only by the embedded cycle counter. */
echoy_band_profile_t echoy_band_render_profile(const echoy_band_t *band);
/* Portable, externally serialized owner of three instruments. Preparation is
 * the one operation performed outside the owner's lock, on an immutable job. */
echoy_band_t *echoy_band_create(void);
void echoy_band_delete(echoy_band_t *band);
bool echoy_band_action(echoy_band_t *band, echoy_tile_id_t tile, echoy_action_t action,
                       double value, int64_t now_us);
bool echoy_band_set_edges(echoy_band_t *band, const echoy_arrange_edge_t *edges, size_t count,
                          int64_t now_us);
/* Hardware: local A follows an inaudible B replica of the physical left tile.
 * NULL plan means a connected peer has not delivered a complete snapshot yet.
 * origin_us is already translated to this device's monotonic timeline. */
bool echoy_band_set_upstream(echoy_band_t *band, bool connected, const echoy_plan_t *plan,
                             bool clock_started, int64_t origin_us, bool playing, int64_t now_us);
/* Establish a shared clock on a connected left tile, without starting playback. */
bool echoy_band_establish_clock(echoy_band_t *band, int64_t now_us);
void echoy_band_tick(echoy_band_t *band, int64_t now_us);
bool echoy_band_read(echoy_band_t *band, echoy_tile_id_t tile, echoy_control_view_t *out);
const echoy_arrange_t *echoy_band_arrangement(const echoy_band_t *band);
echoy_band_job_t *echoy_band_take_job(echoy_band_t *band);
/* No access to band: safe on a worker while UI/audio continue. */
bool echoy_band_prepare_job(echoy_band_job_t *job);
/* Optional device output calibration on an owned, prepared job; once only,
 * before finish_job. Gains follow Pulse/Voice/Dust/Harmony and must be 0..4.
 * Scales musical FULL buffers only; shared synthesis and cue buffers are unchanged. */
bool echoy_band_calibrate_job(echoy_band_job_t *job, const float role_gains[4]);
/* Consumes and releases job even when stale/failed. */
bool echoy_band_finish_job(echoy_band_t *band, echoy_band_job_t *job, int64_t now_us);
void echoy_band_discard_job(echoy_band_job_t *job);
/* Additive scheduling uses absolute microseconds; every tile shares this
 * sample block. Output is mono float, unclipped, ready for the device adapter. */
void echoy_band_render(echoy_band_t *band, int64_t first_sample_us, float *out, size_t frames);
