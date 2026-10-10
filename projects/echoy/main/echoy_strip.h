#pragma once
#include "echoy_model.h"
typedef enum {
    ECHOY_STRIP_IDLE, ECHOY_STRIP_CANCEL, ECHOY_STRIP_PAUSE, ECHOY_STRIP_RECORD_ONLY
} echoy_strip_mode_t;
typedef struct {
    echoy_strip_mode_t mode;
    bool record_enabled, play_enabled, single_enabled;
} echoy_strip_state_t;
/* Presentation permissions only; the existing musical/capture model remains
 * authoritative. A parameter-only preparation is not a playback request. */
echoy_strip_state_t echoy_strip_compute(const echoy_snapshot_t *state, bool has_plan,
                                        bool preparing);

/* CSS ease-out cubic-bezier(0,0,.58,1), including reversing-shortening for
 * interrupted transitions. Pure presentation math, independent of audio time. */
#define ECHOY_STRIP_TWEENS 18
typedef struct {
    double from, to, reverse_start, reverse_factor;
    int64_t at_us, duration_us;
} echoy_strip_tween_t;
typedef struct {
    echoy_strip_tween_t values[ECHOY_STRIP_TWEENS];
    echoy_strip_mode_t mode;
    bool reduced;
} echoy_strip_motion_t;
typedef struct {
    double boundary, record_alpha, play_alpha, idle_alpha, single_alpha, divider_alpha;
    double play_glyph_alpha;
    double shift, scale;
    uint32_t record_rgb, play_rgb, border_rgb;
    bool active;
} echoy_strip_frame_t;
double echoy_strip_ease(double progress);
void echoy_strip_motion_init(echoy_strip_motion_t *motion, echoy_strip_mode_t mode, int64_t now_us);
void echoy_strip_motion_to(echoy_strip_motion_t *motion, echoy_strip_mode_t mode, bool reduced,
                           int64_t now_us);
void echoy_strip_motion_frame(const echoy_strip_motion_t *motion, int64_t now_us,
                              echoy_strip_frame_t *out);
