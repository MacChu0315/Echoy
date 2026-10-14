#include "echoy_strip.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    echoy_snapshot_t s = {.phase = ECHOY_IDLE};
    echoy_strip_state_t v = echoy_strip_compute(&s, false, false);
    assert(v.record_enabled && !v.play_enabled && !v.single_enabled);
    echoy_strip_motion_t empty_motion;
    echoy_strip_frame_t empty_frame;
    echoy_strip_motion_init(&empty_motion, v.mode, 0);
    echoy_strip_motion_frame(&empty_motion, 0, &empty_frame);
    /* An empty tile must devote the whole strip to its first recording. */
    assert(empty_frame.boundary == 244 && empty_frame.divider_alpha == 0);
    assert(131 - empty_frame.shift + 48 == 240);
    assert(empty_frame.idle_alpha == 1 && empty_frame.play_glyph_alpha == 0);
    s.following = true;
    v = echoy_strip_compute(&s, false, false);
    assert(!v.record_enabled && !v.play_enabled);
    echoy_strip_motion_init(&empty_motion, v.mode, 0);
    echoy_strip_motion_frame(&empty_motion, 0, &empty_frame);
    assert(empty_frame.boundary == 0 && empty_frame.divider_alpha == 0);
    assert(empty_frame.idle_alpha == 0 && empty_frame.play_glyph_alpha == 1);
    assert(253 + empty_frame.shift + 48 == 240);
    s.following = false;
    v = echoy_strip_compute(&s, false, true);
    assert(v.mode == ECHOY_STRIP_RECORD_ONLY && !v.record_enabled && !v.single_enabled);
    v = echoy_strip_compute(&s, true, false);
    assert(v.record_enabled && v.play_enabled);
    s.following = true;
    v = echoy_strip_compute(&s, true, false);
    assert(!v.record_enabled && v.play_enabled);
    echoy_strip_motion_init(&empty_motion, v.mode, 0);
    echoy_strip_motion_frame(&empty_motion, 0, &empty_frame);
    assert(empty_frame.boundary == 0 && empty_frame.divider_alpha == 0);
    assert(empty_frame.idle_alpha == 0 && empty_frame.play_glyph_alpha == 1);
    assert(253 + empty_frame.shift + 48 == 240);
    s.following = false;
    s.plan_pending = true;
    v = echoy_strip_compute(&s, true, false);
    assert(v.mode == ECHOY_STRIP_IDLE && !v.record_enabled && !v.play_enabled && !v.single_enabled);
    s.plan_pending = false;
    v = echoy_strip_compute(&s, true, true);
    assert(v.mode == ECHOY_STRIP_IDLE && !v.record_enabled && !v.play_enabled);
    for (int phase = ECHOY_COUNT_IN; phase <= ECHOY_PLAYING; phase++) {
        s.phase = phase;
        v = echoy_strip_compute(&s, true, false);
        assert(v.mode == (phase <= ECHOY_RECORDING ? ECHOY_STRIP_CANCEL : ECHOY_STRIP_PAUSE));
        assert(!v.record_enabled && !v.play_enabled && v.single_enabled);
    }
    s.fault = true;
    v = echoy_strip_compute(&s, true, false);
    assert(!v.record_enabled && !v.play_enabled && !v.single_enabled);
    echoy_model_handle_t m = echoy_model_create();
    assert(m);
    assert(!echoy_model_cancel_record(m));
    echoy_model_get_snapshot(m, &s);
    assert(s.phase == ECHOY_IDLE);
    assert(echoy_model_record(m, 0));
    assert(echoy_model_cancel_record(m));
    echoy_model_get_snapshot(m, &s);
    assert(s.phase == ECHOY_IDLE && s.notice == ECHOY_NOTICE_CANCELLED);
    assert(echoy_strip_compute(&s, false, false).mode == ECHOY_STRIP_RECORD_ONLY);
    assert(!echoy_model_cancel_record(m));
    echoy_model_tick(m, 10000000);
    echoy_model_get_snapshot(m, &s);
    assert(s.phase == ECHOY_IDLE);
    echoy_model_delete(m);
    echoy_strip_motion_t motion;
    echoy_strip_frame_t before, after;
    echoy_strip_motion_init(&motion, ECHOY_STRIP_IDLE, 0);
    echoy_strip_motion_to(&motion, ECHOY_STRIP_CANCEL, false, 0);
    echoy_strip_motion_frame(&motion, 95000, &before);
    assert(before.boundary > 122 && before.boundary < 244 && before.active);
    assert(before.idle_alpha < before.single_alpha);
    echoy_strip_motion_to(&motion, ECHOY_STRIP_IDLE, false, 95000);
    echoy_strip_motion_frame(&motion, 95000, &after);
    assert(before.boundary == after.boundary); /* no jump on reversal */
    echoy_strip_motion_frame(&motion, 400000, &after);
    assert(after.boundary == 122 && !after.active);
    echoy_strip_motion_to(&motion, ECHOY_STRIP_PAUSE, true, 400000);
    echoy_strip_motion_frame(&motion, 400000, &after);
    assert(after.boundary == 0 && !after.active && after.single_alpha == 1);
    /* Returning from Cancel to an empty tile never briefly exposes Play. */
    echoy_strip_motion_init(&motion, ECHOY_STRIP_RECORD_ONLY, 0);
    echoy_strip_motion_to(&motion, ECHOY_STRIP_CANCEL, false, 0);
    echoy_strip_motion_frame(&motion, 90000, &before);
    assert(before.play_glyph_alpha == 0);
    echoy_strip_motion_to(&motion, ECHOY_STRIP_RECORD_ONLY, false, 90000);
    echoy_strip_motion_frame(&motion, 90000, &after);
    assert(before.shift == after.shift && after.play_glyph_alpha == 0);
    echoy_strip_motion_frame(&motion, 400000, &after);
    assert(after.boundary == 244 && after.shift == -61 && after.play_glyph_alpha == 0);
    /* A real phrase restores both halves, including reduced-motion mode. */
    echoy_strip_motion_to(&motion, ECHOY_STRIP_IDLE, true, 400000);
    echoy_strip_motion_frame(&motion, 400000, &after);
    assert(after.boundary == 122 && after.play_glyph_alpha == 1 && !after.active);
    puts("PASS merged strip permissions and stale cancel never re-arms capture");
}
