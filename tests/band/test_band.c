#include "echoy_band.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void prepare(echoy_band_t *b, int64_t now)
{
    echoy_band_job_t *j = echoy_band_take_job(b);
    assert(j);
    assert(echoy_band_prepare_job(j));
    assert(echoy_band_finish_job(b, j, now));
    echoy_band_tick(b, now + 45000);
}
static double energy(echoy_band_t *b, int64_t at, size_t n)
{
    float *v = calloc(n, sizeof(float));
    assert(v);
    echoy_band_render(b, at, v, n);
    double e = 0;
    for (size_t i = 0; i < n; i++) {
        assert(isfinite(v[i]));
        e += v[i] * v[i];
    }
    free(v);
    return e;
}
static void merged_cancel_stops_future_audio(void)
{
    echoy_band_t*b=echoy_band_create();assert(b);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,0));
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_CANCEL_RECORD,0,40000));
    assert(!echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_CANCEL_RECORD,0,50000));
    assert(energy(b,60000,60000)==0); /* all six queued cues are gone */
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,3000000));
    assert(energy(b,3060000,600)>0); /* the real guide is audible before Cancel */
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_CANCEL_RECORD,0,3100000));
    assert(energy(b,3140000,60000)==0);
    echoy_control_view_t view;echoy_band_read(b,ECHOY_TILE_A,&view);
    assert(view.state.phase==ECHOY_IDLE&&view.state.seed==0&&view.state.notice==ECHOY_NOTICE_CANCELLED);
    echoy_band_delete(b);
}

int main(void)
{
    merged_cancel_stops_future_audio();
    echoy_band_t *b = echoy_band_create();
    assert(b);
    echoy_control_view_t v;
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_EXAMPLE, 0, 0));
    prepare(b, 0);
    assert(echoy_band_read(b, ECHOY_TILE_A, &v) && v.plan.count > 0 &&
           v.state.phase == ECHOY_PREVIEW);
    echoy_band_tick(b, 3000000);
    assert(energy(b, 3000000, 2400) == 0);
    assert(echoy_band_action(b, ECHOY_TILE_B, ECHOY_ACT_PRESENT, 1, 3000000));
    echoy_arrange_edge_t edge = {ECHOY_TILE_A, ECHOY_TILE_B};
    assert(echoy_band_set_edges(b, &edge, 1, 3000000));
    prepare(b, 3000000);
    assert(echoy_band_read(b, ECHOY_TILE_B, &v) && v.source == ECHOY_TILE_A && !v.state.continuous);
    assert(!echoy_band_action(b, ECHOY_TILE_B, ECHOY_ACT_RECORD, 0, 3100000));
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, 3100000));
    echoy_band_tick(b, 3100000);
    assert(echoy_band_action(b, ECHOY_TILE_B, ECHOY_ACT_TRANSPORT, 0, 3200000));
    echoy_band_tick(b, 3200000);
    echoy_control_view_t a;
    echoy_band_read(b, ECHOY_TILE_A, &a);
    echoy_band_read(b, ECHOY_TILE_B, &v);
    assert(v.state.clock_origin_us == a.state.clock_origin_us);
    assert((v.state.start_us - a.state.clock_origin_us) % 2500000 == 0);
    assert(energy(b, a.state.start_us, 4800) > 0.001);
    echoy_plan_t old = v.plan;
    assert(echoy_band_set_edges(b, NULL, 0, 4000000));
    prepare(b, 4000000);
    echoy_band_tick(b, 9000000);
    echoy_band_read(b, ECHOY_TILE_B, &v);
    assert(v.source == ECHOY_TILE_NONE && v.state.continuous && v.plan.count == old.count);
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, 9000000));
    echoy_band_read(b, ECHOY_TILE_B, &v);
    assert(v.state.continuous);
    assert(echoy_band_action(b, ECHOY_TILE_B, ECHOY_ACT_CONDITION, ECHOY_AUDIO_FAULT, 9000000));
    assert(echoy_band_action(b, ECHOY_TILE_B, ECHOY_ACT_CONDITION, ECHOY_NORMAL, 9020000));
    echoy_band_read(b, ECHOY_TILE_B, &v);
    assert(v.state.phase == ECHOY_IDLE);
    assert(energy(b, 9050000, 2400) == 0);
    /* Same onsets are still a new completed recording, with one reply. */
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, 10000000));
    echoy_band_read(b, ECHOY_TILE_A, &a);
    int64_t capture = a.state.capture_at_us;
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_TAP, 0, capture));
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_TAP, 0, capture + 312500));
    echoy_band_tick(b, capture + 2500000);
    prepare(b, capture + 2500000);
    echoy_band_read(b, ECHOY_TILE_A, &a);
    assert(a.state.phase == ECHOY_PREVIEW && !a.state.plan_pending);
    /* Replacing a pending automatic reply by a linked job must stay silent. */
    echoy_band_tick(b, capture + 6000000);
    assert(echoy_band_action(b, ECHOY_TILE_B, ECHOY_ACT_EXAMPLE, 0, capture + 6000000));
    echoy_band_job_t *stale = echoy_band_take_job(b);
    assert(stale);
    assert(echoy_band_set_edges(b, &edge, 1, capture + 6010000));
    assert(echoy_band_prepare_job(stale));
    assert(!echoy_band_finish_job(b, stale, capture + 6020000));
    prepare(b, capture + 6030000);
    echoy_band_read(b, ECHOY_TILE_B, &v);
    assert(v.source == ECHOY_TILE_A && v.state.phase == ECHOY_IDLE);
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_LANGUAGE, 1, capture + 7000000));
    for (int i = 0; i < 3; i++) {
        echoy_band_read(b, i, &v);
        assert(v.english);
    }
    echoy_band_delete(b);
    puts("PASS shared clock, audible buffers, link/detach, independent pause and recovery");
    return 0;
}
