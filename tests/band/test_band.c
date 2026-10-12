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
static void three_second_cues_and_unchanged_recording_beats(void)
{
    echoy_band_t *b = echoy_band_create();
    assert(b && echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, 0));
    const size_t n = 144000; /* Six seconds, including silence after the four recording beats. */
    float *pcm = calloc(n, sizeof(*pcm));
    assert(pcm);
    echoy_band_render(b, 0, pcm, n);
    /* Three 1-second preparation cues, then four unchanged 625 ms recording beats. */
    const size_t starts[] = {1440, 25440, 49440, 73440, 88440, 103440, 118440};
    for (size_t cue = 0; cue < 7; cue++) {
        double power = 0;
        unsigned crossings = 0;
        for (size_t i = 0; i < 720; i++) power += pcm[starts[cue] + i] * pcm[starts[cue] + i];
        assert(power > .00001);
        for (size_t i = 1; i < 480; i++)
            crossings += pcm[starts[cue] + i - 1] <= 0 && pcm[starts[cue] + i] > 0;
        assert(cue == 3 ? (crossings >= 29 && crossings <= 31)
                        : (crossings >= 20 && crossings <= 22));
    }
    for (size_t i = 0; i < n; i++) {
        bool inside = false;
        for (size_t cue = 0; cue < 7; cue++)
            inside |= i >= starts[cue] && i < starts[cue] + 720;
        if (!inside) assert(pcm[i] == 0); /* No leftover old-tempo preparation cues. */
    }
    free(pcm);
    echoy_band_delete(b);
}

static void default_expressions_and_role_memory(void)
{
    echoy_band_t *b = echoy_band_create();
    assert(b);
    prepare(b, 0);
    echoy_control_view_t v;
    for (int tile = 0; tile < 3; tile++) {
        assert(echoy_band_read(b, (echoy_tile_id_t)tile, &v));
        assert(v.state.phrase == 1);
        for (int role = 0; role < 4; role++) assert(v.memories[role] == 1);
    }
    int64_t now = 100000;
    for (int role = ECHOY_PULSE; role <= ECHOY_HARMONY; role++) {
        assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_ROLE, role, now));
        prepare(b, now);
        assert(echoy_band_read(b, ECHOY_TILE_A, &v) && v.state.phrase == 1);
        assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 3, now + 100000));
        prepare(b, now + 100000);
        now += 200000;
    }
    for (int role = ECHOY_PULSE; role <= ECHOY_HARMONY; role++) {
        assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_ROLE, role, now));
        prepare(b, now);
        assert(echoy_band_read(b, ECHOY_TILE_A, &v) && v.state.phrase == 3);
        now += 100000;
    }
    echoy_band_delete(b);
}

static void merged_cancel_stops_future_audio(void)
{
    echoy_band_t*b=echoy_band_create();assert(b);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,0));
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_CANCEL_RECORD,0,40000));
    assert(!echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_CANCEL_RECORD,0,50000));
    assert(energy(b,60000,144000)==0); /* all seven queued cues are gone */
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,7000000));
    assert(energy(b,7060000,600)>0); /* the real guide is audible before Cancel */
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_CANCEL_RECORD,0,7100000));
    assert(energy(b,7140000,144000)==0);
    echoy_control_view_t view;echoy_band_read(b,ECHOY_TILE_A,&view);
    assert(view.state.phase==ECHOY_IDLE&&view.state.seed==0&&view.state.notice==ECHOY_NOTICE_CANCELLED);
    echoy_band_delete(b);
}

int main(void)
{
    default_expressions_and_role_memory();
    three_second_cues_and_unchanged_recording_beats();
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
