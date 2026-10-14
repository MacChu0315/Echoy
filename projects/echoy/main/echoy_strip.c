#include "echoy_strip.h"
echoy_strip_state_t echoy_strip_compute(const echoy_snapshot_t *s, bool has_plan, bool preparing)
{
    echoy_strip_state_t out = {0};
    if (!s) {
        return out;
    }
    out.mode = s->following ? ECHOY_STRIP_PLAY_ONLY : has_plan ? ECHOY_STRIP_IDLE : ECHOY_STRIP_RECORD_ONLY;
    if (s->phase == ECHOY_COUNT_IN || s->phase == ECHOY_RECORDING) {
        out.mode = ECHOY_STRIP_CANCEL;
    } else if (s->phase >= ECHOY_PREPARING) {
        out.mode = ECHOY_STRIP_PAUSE;
    }
    if (s->fault) {
        return out;
    }
    out.single_enabled = out.mode == ECHOY_STRIP_CANCEL || out.mode == ECHOY_STRIP_PAUSE;
    if (!out.single_enabled && !s->plan_pending && !preparing) {
        out.record_enabled = !s->following;
        out.play_enabled = has_plan;
    }
    return out;
}

#include <math.h>
#include <string.h>
enum {
    BOUNDARY,
    RECORD_ALPHA,
    PLAY_ALPHA,
    IDLE_ALPHA,
    SINGLE_ALPHA,
    DIVIDER_ALPHA,
    SHIFT,
    SCALE,
    RECORD_R,
    RECORD_G,
    RECORD_B,
    PLAY_R,
    PLAY_G,
    PLAY_B,
    BORDER_R,
    BORDER_G,
    BORDER_B,
    PLAY_GLYPH_ALPHA
};
static double clamp(double x)
{
    return x < 0 ? 0 : x > 1 ? 1 : x;
}
double echoy_strip_ease(double x)
{
    if (x <= 0) {
        return 0;
    }
    if (x >= 1) {
        return 1;
    }
    double lo = 0, hi = 1;
    for (unsigned i = 0; i < 40; i++) {
        double u = (lo + hi) * .5, at = 1.74 * u * u - .74 * u * u * u;
        if (at < x) {
            lo = u;
        } else {
            hi = u;
        }
    }
    double u = (lo + hi) * .5;
    return 3 * u * u - 2 * u * u * u;
}
static double progress(const echoy_strip_tween_t *t, int64_t now)
{
    return t->duration_us > 0 ? clamp((now - t->at_us) / (double)t->duration_us) : 1;
}
static double value(const echoy_strip_tween_t *t, int64_t now)
{
    double e = echoy_strip_ease(progress(t, now));
    return t->from + (t->to - t->from) * e;
}
static void colors(double *values, unsigned first, uint32_t rgb)
{
    values[first] = (rgb >> 16) & 255;
    values[first + 1] = (rgb >> 8) & 255;
    values[first + 2] = rgb & 255;
}
static void targets(echoy_strip_mode_t mode, double out[ECHOY_STRIP_TWEENS])
{
    bool split = mode == ECHOY_STRIP_IDLE, empty = mode == ECHOY_STRIP_RECORD_ONLY;
    bool play_only = mode == ECHOY_STRIP_PLAY_ONLY;
    bool idle = split || empty || play_only, cancel = mode == ECHOY_STRIP_CANCEL;
    out[BOUNDARY] = split ? 122 : cancel || empty ? 244 : 0;
    out[RECORD_ALPHA] = mode == ECHOY_STRIP_PAUSE || play_only ? 0 : 1;
    out[PLAY_ALPHA] = cancel || empty ? 0 : 1;
    out[IDLE_ALPHA] = split || empty ? 1 : 0;
    out[PLAY_GLYPH_ALPHA] = split || play_only ? 1 : 0;
    out[SINGLE_ALPHA] = idle ? 0 : 1;
    out[DIVIDER_ALPHA] = split ? 1 : 0;
    out[SHIFT] = empty || play_only ? -61 : split ? 0 : 8;
    out[SCALE] = idle ? .97 : 1;
    colors(out, RECORD_R, cancel ? 0xf0e1d7 : 0xeee8df);
    colors(out, PLAY_R, mode == ECHOY_STRIP_PAUSE ? 0x455d38 : 0xe8ede1);
    colors(out, BORDER_R, idle ? 0xd2d9c9 : cancel ? 0xd5a98d : 0x455b3b);
}
void echoy_strip_motion_init(echoy_strip_motion_t *m, echoy_strip_mode_t mode, int64_t now)
{
    memset(m, 0, sizeof(*m));
    double end[ECHOY_STRIP_TWEENS];
    targets(mode, end);
    m->mode = mode;
    for (unsigned i = 0; i < ECHOY_STRIP_TWEENS; i++) {
        m->values[i] = (echoy_strip_tween_t){.from = end[i],
                                             .to = end[i],
                                             .reverse_start = end[i],
                                             .reverse_factor = 1,
                                             .at_us = now};
    }
}
void echoy_strip_motion_to(echoy_strip_motion_t *m, echoy_strip_mode_t mode, bool reduced,
                           int64_t now)
{
    static const int duration[ECHOY_STRIP_TWEENS] = {240, 200, 200, 130, 200, 150, 240, 240, 240,
                                                     240, 240, 240, 240, 240, 240, 240, 240, 130};
    double end[ECHOY_STRIP_TWEENS];
    targets(mode, end);
    for (unsigned i = 0; i < ECHOY_STRIP_TWEENS; i++) {
        echoy_strip_tween_t *t = &m->values[i];
        if (reduced) {
            *t = (echoy_strip_tween_t){.from = end[i],
                                       .to = end[i],
                                       .reverse_start = end[i],
                                       .reverse_factor = 1,
                                       .at_us = now};
            continue;
        }
        if (t->to == end[i]) {
            continue;
        }
        double current = value(t, now), factor = 1, reverse_start = current;
        if (t->reverse_start == end[i] && progress(t, now) < 1) {
            factor = clamp(fabs(echoy_strip_ease(progress(t, now)) * t->reverse_factor + 1 -
                                t->reverse_factor));
            reverse_start = t->to;
        }
        *t = (echoy_strip_tween_t){.from = current,
                                   .to = end[i],
                                   .reverse_start = reverse_start,
                                   .reverse_factor = factor,
                                   .at_us = now,
                                   .duration_us = (int64_t)llround(duration[i] * 1000 * factor)};
    }
    m->mode = mode;
    m->reduced = reduced;
}
static uint32_t rgb(const double *v, unsigned first)
{
    return (uint32_t)lround(v[first]) << 16 | (uint32_t)lround(v[first + 1]) << 8 |
           (uint32_t)lround(v[first + 2]);
}
void echoy_strip_motion_frame(const echoy_strip_motion_t *m, int64_t now, echoy_strip_frame_t *out)
{
    double v[ECHOY_STRIP_TWEENS];
    memset(out, 0, sizeof(*out));
    for (unsigned i = 0; i < ECHOY_STRIP_TWEENS; i++) {
        v[i] = value(&m->values[i], now);
        if (progress(&m->values[i], now) < 1) {
            out->active = true;
        }
    }
    out->boundary = v[BOUNDARY];
    out->record_alpha = v[RECORD_ALPHA];
    out->play_alpha = v[PLAY_ALPHA];
    out->idle_alpha = v[IDLE_ALPHA];
    out->play_glyph_alpha = v[PLAY_GLYPH_ALPHA];
    out->single_alpha = v[SINGLE_ALPHA];
    out->divider_alpha = v[DIVIDER_ALPHA];
    out->shift = v[SHIFT];
    out->scale = v[SCALE];
    out->record_rgb = rgb(v, RECORD_R);
    out->play_rgb = rgb(v, PLAY_R);
    out->border_rgb = rgb(v, BORDER_R);
}
