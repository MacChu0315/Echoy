#include "echoy_model.h"
#include <assert.h>
#include <stdio.h>

static echoy_snapshot_t snap(echoy_model_handle_t m)
{
    echoy_snapshot_t s;
    echoy_model_get_snapshot(m, &s);
    return s;
}

static void record_seed(echoy_model_handle_t m)
{
    assert(echoy_model_record(m, 0));
    echoy_model_tap(m, 1310000 + 625000);
    echoy_model_tick(m, 3810000);
    assert(snap(m).seed == (1u << 4));
}

static void count_in_and_leading_silence(void)
{
    echoy_model_handle_t m = echoy_model_create();
    assert(m);
    assert(echoy_model_record(m, 0));
    assert(snap(m).phase == ECHOY_COUNT_IN);
    echoy_model_tap(m, 100000);
    assert(snap(m).pending_seed == 0);
    echoy_model_tick(m, 1310000);
    assert(snap(m).phase == ECHOY_RECORDING);
    echoy_model_tap(m, 1935000);
    echoy_model_tick(m, 3810000);
    assert(snap(m).seed == (1u << 4));
    assert(snap(m).phase == ECHOY_PREPARING);
    assert(!snap(m).continuous);
    echoy_model_delete(m);
}

static void ordinary_taps_do_not_record(void)
{
    echoy_model_handle_t m = echoy_model_create();
    assert(echoy_model_tap(m, 0));
    assert(echoy_model_tap(m, 100000));
    assert(snap(m).seed == 0 && snap(m).pending_seed == 0);
    assert(snap(m).phase == ECHOY_IDLE);
    echoy_model_delete(m);
}

static void empty_and_cancel_keep_seed(void)
{
    echoy_model_handle_t m = echoy_model_create();
    record_seed(m);
    echoy_model_pause(m);
    assert(echoy_model_record(m, 5000000));
    echoy_model_tick(m, 8810000);
    assert(snap(m).phase == ECHOY_IDLE && snap(m).seed == (1u << 4));
    assert(echoy_model_record(m, 10000000));
    echoy_model_tap(m, 11310000);
    assert(echoy_model_record(m, 11320000));
    assert(snap(m).phase == ECHOY_IDLE && snap(m).seed == (1u << 4));
    assert(snap(m).pending_seed == 0);
    echoy_model_delete(m);
}

static void quantization_boundaries_and_duplicates(void)
{
    echoy_model_handle_t m = echoy_model_create();
    assert(echoy_model_record(m, 0));
    echoy_model_tap(m, 1309999);
    assert(snap(m).pending_seed == 0);
    echoy_model_tap(m, 1310000);
    echoy_model_tap(m, 1310001);
    echoy_model_tap(m, 1388125);
    echoy_model_tap(m, 3809999);
    echoy_model_tap(m, 3810000);
    echoy_model_tick(m, 3810000);
    assert(snap(m).seed == ((1u << 0) | (1u << 1) | (1u << 15)));
    echoy_model_delete(m);
}

static void preview_once_and_stale_prepare(void)
{
    echoy_model_handle_t m = echoy_model_create();
    record_seed(m);
    uint32_t token = snap(m).generation;
    assert(echoy_model_prepare_done(m, token, 4000000));
    assert(snap(m).phase == ECHOY_PREVIEW);
    echoy_model_tick(m, 6550000);
    assert(snap(m).phase == ECHOY_IDLE);
    assert(echoy_model_transport(m, 7000000));
    token = snap(m).generation;
    echoy_model_pause(m);
    assert(!echoy_model_prepare_done(m, token, 7100000));
    assert(snap(m).phase == ECHOY_IDLE);
    echoy_model_delete(m);
}

static void pause_keeps_clock_and_resume_aligns(void)
{
    echoy_model_handle_t m = echoy_model_create();
    record_seed(m);
    echoy_model_pause(m);
    assert(echoy_model_transport(m, 4000000));
    assert(echoy_model_prepare_done(m, snap(m).generation, 4000000));
    assert(snap(m).phase == ECHOY_PLAYING && snap(m).start_us == 4050000);
    int64_t origin = snap(m).clock_origin_us;
    echoy_model_pause(m);
    assert(snap(m).clock_origin_us == origin);
    assert(echoy_model_transport(m, 8000000));
    assert(echoy_model_prepare_done(m, snap(m).generation, 8000000));
    assert(snap(m).start_us == 9050000 && snap(m).clock_origin_us == origin);
    echoy_model_delete(m);
}

static void fault_and_left_neighbor_cancel(void)
{
    echoy_model_handle_t m = echoy_model_create();
    record_seed(m);
    echoy_model_pause(m);
    assert(echoy_model_record(m, 5000000));
    echoy_model_set_following(m, true);
    assert(snap(m).phase == ECHOY_IDLE && snap(m).seed == (1u << 4));
    assert(!echoy_model_record(m, 6000000));
    echoy_model_set_following(m, false);
    assert(echoy_model_transport(m, 7000000));
    uint32_t token = snap(m).generation;
    echoy_model_set_fault(m, true);
    assert(!echoy_model_tap(m, 7000100));
    assert(!echoy_model_prepare_done(m, token, 7000200));
    echoy_model_set_fault(m, false);
    assert(snap(m).phase == ECHOY_IDLE && snap(m).seed == (1u << 4));
    echoy_model_delete(m);
}

int main(void)
{
    count_in_and_leading_silence();
    puts("PASS count-in and leading silence");
    ordinary_taps_do_not_record();
    puts("PASS audition-only taps");
    empty_and_cancel_keep_seed();
    puts("PASS empty/cancel preservation");
    quantization_boundaries_and_duplicates();
    puts("PASS boundary quantization");
    preview_once_and_stale_prepare();
    puts("PASS one-shot and stale completion");
    pause_keeps_clock_and_resume_aligns();
    puts("PASS persistent musical clock");
    fault_and_left_neighbor_cancel();
    puts("PASS fault and following cancellation");
    return 0;
}
