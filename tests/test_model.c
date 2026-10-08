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

static void composed_reply_and_role_memory(void)
{
    echoy_model_handle_t m = echoy_model_create();
    record_seed(m);
    assert(echoy_model_prepare_done(m, snap(m).generation, 4000000));
    const echoy_plan_t *plan = echoy_model_get_plan(m);
    assert(plan && plan->source_count == 1 && plan->count > 1);
    assert(plan->source[0].step == 4);
    for (size_t i = 0; i < plan->count; ++i) {
        assert(plan->events[i].step >= 4);
    }
    echoy_model_pause(m);
    assert(echoy_model_select_role(m, ECHOY_HARMONY, 5000000));
    assert(snap(m).phrase == 2);
    echoy_model_tick(m, 5050000);
    assert(echoy_model_select_phrase(m, 3, 5100000));
    echoy_model_tick(m, 5150000);
    assert(echoy_model_select_role(m, ECHOY_VOICE, 5200000));
    assert(snap(m).phrase == 1);
    assert(echoy_model_select_role(m, ECHOY_HARMONY, 5300000));
    assert(snap(m).phrase == 3);
    echoy_model_tick(m, 5350000);
    assert(echoy_model_get_plan(m)->role == ECHOY_HARMONY);
    echoy_model_delete(m);
}

static void configuration_commits_at_bar_boundary(void)
{
    echoy_model_handle_t m = echoy_model_create();
    record_seed(m);
    echoy_model_pause(m);
    assert(echoy_model_transport(m, 4000000));
    assert(echoy_model_prepare_done(m, snap(m).generation, 4000000));
    assert(echoy_model_select_role(m, ECHOY_VOICE, 5000000));
    assert(snap(m).plan_pending && snap(m).apply_at_us == 6550000);
    assert(echoy_model_get_plan(m)->role == ECHOY_PULSE);
    echoy_model_tick(m, 6549999);
    assert(echoy_model_get_plan(m)->role == ECHOY_PULSE);
    echoy_model_tick(m, 6550000);
    assert(echoy_model_get_plan(m)->role == ECHOY_VOICE);
    assert(snap(m).phase == ECHOY_PLAYING && snap(m).clock_origin_us == 4050000);
    assert(echoy_model_set_density(m, 1.0, 7000000));
    echoy_model_pause(m);
    echoy_model_tick(m, 9050000);
    assert(snap(m).phase == ECHOY_IDLE && echoy_model_get_plan(m)->density == 1.0);
    echoy_model_delete(m);
}

static void preview_keeps_old_plan_until_its_end(void)
{
    echoy_model_handle_t m = echoy_model_create();
    record_seed(m);
    assert(echoy_model_prepare_done(m, snap(m).generation, 4000000));
    assert(echoy_model_select_phrase(m, 3, 5000000));
    assert(snap(m).apply_at_us == 6550000);
    echoy_model_tick(m, 6000000);
    assert(echoy_model_get_plan(m)->phrase == 1 && snap(m).phase == ECHOY_PREVIEW);
    echoy_model_tick(m, 6550000);
    assert(echoy_model_get_plan(m)->phrase == 3 && snap(m).phase == ECHOY_IDLE);
    echoy_model_delete(m);
}

static void managed_plan_waits_for_audio_and_external_clock(void)
{
    echoy_model_handle_t m = echoy_model_create_managed();
    assert(m);
    assert(echoy_model_load_seed(m, 0x44a0, 0, true));
    echoy_model_tick(m, 10000000);
    assert(snap(m).phase == ECHOY_PREPARING && snap(m).plan_pending);
    assert(!echoy_model_get_plan(m)->count);
    echoy_plan_t plan;
    assert(echoy_compose_mask(0x44a0, ECHOY_HARMONY, 2, .5, &plan));
    echoy_model_apply_external(m, &plan, 42, false, 1000000, true);
    uint32_t generation = snap(m).generation;
    assert(!echoy_model_start_external(m, generation + 1, 11000000));
    assert(echoy_model_start_external(m, generation, 11000000));
    assert(snap(m).phase == ECHOY_PREVIEW && snap(m).start_us == 11000000);
    assert(snap(m).clock_origin_us == 1000000 && snap(m).committed_revision == 42);
    echoy_model_tick(m, 13500000);
    assert(snap(m).phase == ECHOY_IDLE);
    echoy_model_set_external_pending(m, true, 16000000);
    assert(!echoy_model_transport(m, 14000000));
    echoy_model_delete(m);
}

int main(void)
{
    managed_plan_waits_for_audio_and_external_clock();
    puts("PASS external plan preparation and clock");
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
    composed_reply_and_role_memory();
    puts("PASS complete reply and per-role phrase memory");
    configuration_commits_at_bar_boundary();
    puts("PASS parameter commit at bar boundary");
    preview_keeps_old_plan_until_its_end();
    puts("PASS preview plan lifetime");
    return 0;
}
