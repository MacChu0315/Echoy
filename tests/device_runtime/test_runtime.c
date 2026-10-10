#include "echoy_band.h"
#include "esp_timer.h"
#include "fakes.h"
#include "platform_device.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static echoy_device_handle_t retained_failure;
static echoy_platform_t p;
static echoy_control_view_t view(void)
{
    echoy_control_view_t v;
    assert(p.read_control(p.ctx, &v));
    return v;
}
static echoy_control_view_t until(int condition, int phase, int seed)
{
    int64_t end = esp_timer_get_time() + 2500000;
    echoy_control_view_t v;
    do {
        v = view();
        if ((condition < 0 || v.condition == condition) && (phase < 0 || v.state.phase == phase) &&
            (seed < 0 || v.state.seed == seed))
            return v;
        fake_sleep_ms(1);
    } while (esp_timer_get_time() < end);
    fprintf(stderr, "timeout condition=%d phase=%d seed=%x (wanted %d/%d/%x)\n", v.condition,
            v.state.phase, v.state.seed, condition, phase, seed);
    abort();
}
static void start(echoy_device_handle_t *d)
{
    assert(echoy_device_start(d, &p) == ESP_OK);
    assert(echoy_device_activate(*d) == ESP_OK);
    assert(*d);
    assert(!p.is_simulator);
    until(ECHOY_NORMAL, ECHOY_IDLE, 0);
}
static bool action(echoy_action_t a, double v)
{
    return p.perform_action(p.ctx, a, v, p.now_us(p.ctx));
}
static void stop(echoy_device_handle_t *d)
{
    assert(echoy_device_stop(d, 2000) == ESP_OK);
    assert(!*d);
    fake_join_tasks();
}
static void block_writer(void)
{
    fake_hold_write(true);
    int64_t end = esp_timer_get_time() + 1000000;
    while (!fake_write_is_blocked()) {
        assert(esp_timer_get_time() < end);
        fake_sleep_ms(1);
    }
}
static void unknown_and_cache(void)
{
    echoy_device_handle_t d;
    fake_set_health(-1);
    start(&d);
    echoy_control_view_t v = view();
    assert(v.battery_unknown && v.imu_unknown && v.condition == ECHOY_NORMAL);
    assert(!action(ECHOY_ACT_PRESENT, 0));
    block_writer();
    int64_t before = esp_timer_get_time();
    for (int i = 0; i < 100; i++) {
        v = view();
        assert(v.present && v.battery_unknown);
    }
    assert(esp_timer_get_time() - before < 5000);
    assert(fake_write_is_blocked());
    fake_hold_write(false);
    stop(&d);
    puts("PASS unknown battery is not fabricated; cache reads remain independent of blocked I/O");
}
static void fifo_cancel(void)
{
    echoy_device_handle_t d;
    start(&d);
    block_writer();
    unsigned generation = view().state.generation;
    assert(action(ECHOY_ACT_RECORD, 0));
    assert(action(ECHOY_ACT_CANCEL_RECORD, 0));
    fake_hold_write(false);
    int64_t end = esp_timer_get_time() + 1000000;
    echoy_control_view_t v;
    do {
        v = view();
        if (v.state.generation >= generation + 2)
            break;
        fake_sleep_ms(1);
    } while (esp_timer_get_time() < end);
    assert(v.state.generation == generation + 2);
    assert(v.state.phase == ECHOY_IDLE && v.state.seed == 0 && v.state.pending_seed == 0);
    fake_sleep_ms(120);
    assert(view().state.phase == ECHOY_IDLE);
    assert(atomic_load(&fake_nonzero_blocks) == 0);
    assert(atomic_load(&fake_flush_calls) >= 2);
    stop(&d);
    puts("PASS Record then Cancel stays FIFO, flushes DMA and cannot arm a late capture");
}
static void pause_clears_limiter_tail(void)
{
    echoy_device_handle_t d;
    start(&d);
    assert(action(ECHOY_ACT_EXAMPLE, 0));
    until(ECHOY_NORMAL, ECHOY_PREVIEW, 5);
    int64_t end = esp_timer_get_time() + 1000000;
    while (!atomic_load(&fake_nonzero_blocks)) {
        assert(esp_timer_get_time() < end);
        fake_sleep_ms(1);
    }
    block_writer();
    int flushes = atomic_load(&fake_flush_calls);
    assert(action(ECHOY_ACT_PAUSE, 0));
    fake_hold_write(false);
    until(ECHOY_NORMAL, ECHOY_IDLE, 5);
    fake_sleep_ms(40);
    assert(atomic_load(&fake_flush_calls) > flushes);
    assert(atomic_load(&fake_nonzero_since_flush) == 0);
    stop(&d);
    puts("PASS pause clears delayed limiter PCM before the next board write");
}
static void calibration_failure(void)
{
    echoy_device_handle_t d;
    start(&d);
    atomic_store(&fake_calibration_failure,1);
    assert(action(ECHOY_ACT_EXAMPLE,0));
    until(ECHOY_AUDIO_FAULT,ECHOY_IDLE,-1);
    assert(atomic_load(&fake_nonzero_blocks)==0);
    stop(&d);
    puts("PASS failed output calibration discards the new bank and stops audio");
}
static void overflow(void)
{
    echoy_device_handle_t d;
    start(&d);
    block_writer();
    for (int i = 0; i < 32; i++)
        assert(action(ECHOY_ACT_RECORD, 0));
    assert(!action(ECHOY_ACT_CANCEL_RECORD, 0));
    fake_hold_write(false);
    echoy_control_view_t v = until(ECHOY_AUDIO_FAULT, ECHOY_IDLE, 0);
    assert(!v.state.continuous);
    int writes = atomic_load(&fake_write_calls);
    fake_sleep_ms(40);
    assert(atomic_load(&fake_write_calls) == writes);
    assert(action(ECHOY_ACT_CONDITION, ECHOY_NORMAL));
    v = until(ECHOY_NORMAL, ECHOY_IDLE, 0);
    assert(!v.state.continuous);
    fake_sleep_ms(80);
    assert(atomic_load(&fake_nonzero_blocks) == 0);
    assert(atomic_load(&fake_open_calls) == 2);
    stop(&d);
    puts("PASS queue overflow discards old actions, closes output, and explicit recovery stays "
         "silent");
}
static void partial_recovery(void)
{
    echoy_device_handle_t d;
    start(&d);
    assert(action(ECHOY_ACT_EXAMPLE, 0));
    until(ECHOY_NORMAL, ECHOY_PREVIEW, 5);
    atomic_store(&fake_partial_next, 1);
    echoy_control_view_t v = until(ECHOY_AUDIO_FAULT, ECHOY_IDLE, 5);
    assert(!v.state.continuous);
    int writes = atomic_load(&fake_write_calls);
    fake_sleep_ms(30);
    assert(atomic_load(&fake_write_calls) == writes);
    assert(action(ECHOY_ACT_CONDITION, ECHOY_NORMAL));
    v = until(ECHOY_NORMAL, ECHOY_IDLE, 5);
    assert(!v.state.continuous);
    assert(atomic_load(&fake_open_calls) == 2);
    fake_sleep_ms(80);
    assert(view().state.phase == ECHOY_IDLE);
    stop(&d);
    puts("PASS partial PCM acceptance faults output; recovery preserves seed and never resumes "
         "playback");
}
static void empty_battery_latch(void)
{
    echoy_device_handle_t d;
    start(&d);
    fake_set_health(0);
    echoy_control_view_t v = until(ECHOY_BATTERY_EMPTY, ECHOY_IDLE, 0);
    assert(!v.battery_unknown && v.battery == 0);
    fake_set_health(-1);
    fake_sleep_ms(40);
    v = view();
    assert(v.battery_unknown && v.condition == ECHOY_BATTERY_EMPTY);
    assert(action(ECHOY_ACT_CONDITION, ECHOY_NORMAL));
    fake_sleep_ms(30);
    assert(view().condition == ECHOY_BATTERY_EMPTY);
    fake_set_health(25);
    v = until(ECHOY_NORMAL, ECHOY_IDLE, 0);
    assert(v.battery == 25 && !v.battery_unknown && !v.state.continuous);
    stop(&d);
    puts("PASS empty battery remains latched across read failure; validated charge recovery stays "
         "idle");
}
static void startup_timeout(void)
{
    echoy_device_handle_t d;
    fake_hold_create(true);
    assert(echoy_device_start(&d, &p) == ESP_OK);
    assert(echoy_device_activate(d) == ESP_OK);
    int64_t end = esp_timer_get_time() + 1000000;
    while (!fake_create_is_blocked()) {
        assert(esp_timer_get_time() < end);
        fake_sleep_ms(1);
    }
    assert(view().condition == ECHOY_LOADING);
    assert(echoy_device_stop(&d, 5) == ESP_ERR_TIMEOUT && d);
    assert(!action(ECHOY_ACT_RECORD, 0));
    fake_hold_create(false);
    stop(&d);
    assert(!atomic_load(&fake_open_calls));
    puts("PASS stop during startup preserves context until ownership handoff finishes");
}
static void prepare_timeout(void)
{
    echoy_device_handle_t d;
    start(&d);
    fake_hold_prepare(true);
    assert(action(ECHOY_ACT_EXAMPLE, 0));
    int64_t end = esp_timer_get_time() + 1000000;
    while (!fake_prepare_is_blocked()) {
        assert(esp_timer_get_time() < end);
        fake_sleep_ms(1);
    }
    assert(echoy_device_stop(&d, 5) == ESP_ERR_TIMEOUT && d);
    fake_hold_prepare(false);
    stop(&d);
    puts("PASS shutdown waits for immutable job ownership and supports timeout retry");
}
static void task_failure(void)
{
    echoy_device_handle_t d = NULL;
    atomic_store(&fake_task_failure, 2);
    assert(echoy_device_start(&d, &p) == ESP_ERR_NO_MEM);
    assert(!d);
    fake_join_tasks();
    puts("PASS partial task creation joins startup worker and releases its band");
}
static void health_stop_retry(void)
{
    echoy_device_handle_t d;
    start(&d);
    atomic_store(&fake_health_stop_timeout, 1);
    assert(echoy_device_stop(&d, 1000) == ESP_ERR_TIMEOUT && d);
    stop(&d);
    puts("PASS health cleanup timeout preserves a retryable handle");
}
static void close_failure(void)
{
    echoy_device_handle_t d;
    start(&d);
    atomic_store(&fake_close_failure, 1);
    assert(echoy_device_stop(&d, 1000) == ESP_FAIL && d);
    retained_failure = d;
    fake_sleep_ms(20);
    assert(!atomic_load(&fake_event_tail_reads_after_delete));
    puts("PASS shutdown propagates driver cleanup failure and retains uncertain ownership");
}
static void loading_cache_contract(void)
{
    echoy_device_handle_t d;
    fake_hold_create(true);
    assert(echoy_device_start(&d, &p) == ESP_OK);
    echoy_control_view_t v = view();
    assert(v.condition == ECHOY_LOADING && v.state.fault && v.present);
    assert(v.plan.role == ECHOY_PULSE && v.plan.phrase == 1 && v.plan.duration == 2.5);
    for (int role = ECHOY_PULSE; role <= ECHOY_HARMONY; ++role) {
        echoy_plan_t gallery;
        /* The real UI generates its gallery even while the status page is shown. */
        assert(echoy_compose_mask(v.state.seed, role, v.memories[role], v.state.density, &gallery));
    }
    assert(v.battery_unknown && v.imu_unknown);
    fake_hold_create(false);
    assert(echoy_device_activate(d) == ESP_OK);
    until(ECHOY_NORMAL, ECHOY_IDLE, 0);
    stop(&d);
    puts("PASS initial loading cache is a valid input for the real UI's gallery composer");
}
static void open_failure_recovery(void)
{
    echoy_device_handle_t d;
    atomic_store(&fake_open_failure, 1);
    assert(echoy_device_start(&d, &p) == ESP_OK);
    assert(echoy_device_activate(d) == ESP_OK);
    until(ECHOY_AUDIO_FAULT, ECHOY_IDLE, 0);
    fake_sleep_ms(80);
    assert(atomic_load(&fake_open_calls) == 1 && !atomic_load(&fake_write_calls));
    assert(action(ECHOY_ACT_CONDITION, ECHOY_NORMAL));
    until(ECHOY_NORMAL, ECHOY_IDLE, 0);
    assert(atomic_load(&fake_open_calls) == 2);
    assert(!view().state.continuous);
    stop(&d);
    puts("PASS initialization failure never retries or plays until explicit recovery");
}
static void flush_failure(void)
{
    echoy_device_handle_t d;
    start(&d);
    atomic_store(&fake_flush_failure, 1);
    assert(action(ECHOY_ACT_RECORD, 0));
    until(ECHOY_AUDIO_FAULT, ECHOY_IDLE, 0);
    int writes = atomic_load(&fake_write_calls);
    fake_sleep_ms(40);
    assert(atomic_load(&fake_write_calls) == writes);
    assert(atomic_load(&fake_close_calls) == 1);
    stop(&d);
    puts("PASS failed DMA flush closes output and cancels recording");
}
static void missed_deadline(void)
{
    echoy_device_handle_t d;
    start(&d);
    block_writer();
    int writes = atomic_load(&fake_write_calls);
    fake_sleep_ms(80);
    fake_hold_write(false);
    until(ECHOY_AUDIO_FAULT, ECHOY_IDLE, 0);
    fake_sleep_ms(30);
    assert(atomic_load(&fake_write_calls) == writes);
    assert(atomic_load(&fake_close_calls) == 1);
    stop(&d);
    puts("PASS output deadline miss faults instead of writing catch-up blocks");
}
static void acceptance_gate(void)
{
    echoy_device_handle_t d;
    assert(echoy_device_start(&d, &p) == ESP_OK);
    /* Simulate main performing UI setup and blocking flash acceptance work.
     * Audio must not start until that lifecycle work is explicitly complete. */
    fake_sleep_ms(120);
    assert(atomic_load(&fake_open_calls) == 0);
    assert(atomic_load(&fake_write_calls) == 0);
    assert(view().condition == ECHOY_LOADING);
    assert(echoy_device_activate(d) == ESP_OK);
    assert(echoy_device_activate(d) == ESP_ERR_INVALID_STATE);
    until(ECHOY_NORMAL, ECHOY_IDLE, 0);
    assert(atomic_load(&fake_open_calls) == 1);
    stop(&d);
    puts("PASS deferred runtime does not stream while startup flash work is in progress");
}
static void stop_before_activation(void)
{
    echoy_device_handle_t d;
    assert(echoy_device_start(&d, &p) == ESP_OK);
    assert(view().condition == ECHOY_LOADING);
    stop(&d);
    assert(atomic_load(&fake_open_calls) == 0);
    assert(atomic_load(&fake_write_calls) == 0);
    puts("PASS failed UI/acceptance can shut down unopened audio without hanging");
}
static echoy_device_diagnostics_t probe_until(echoy_device_handle_t d, echoy_probe_phase_t phase)
{
    echoy_device_diagnostics_t snapshot;
    int64_t end=esp_timer_get_time()+2500000;
    do {
        assert(echoy_device_probe_get(d,&snapshot)==ESP_OK);
        if(snapshot.phase==phase)return snapshot;
        fake_sleep_ms(1);
    } while(esp_timer_get_time()<end);
    fprintf(stderr,"probe timeout: phase %d wanted %d\n",snapshot.phase,phase);abort();
}
static void probe_complete(void)
{
    echoy_device_handle_t d;
    start(&d);
    assert(atomic_load(&fake_probe_calls)==0);
    assert(action(ECHOY_ACT_EXAMPLE,0));until(ECHOY_NORMAL,ECHOY_PREVIEW,5);
    assert(echoy_device_probe_request(d,true)==ESP_OK);
    assert(echoy_device_probe_request(d,true)==ESP_ERR_INVALID_STATE);
    echoy_device_diagnostics_t p=probe_until(d,ECHOY_PROBE_DONE);
    assert(p.frames_accepted==24000&&p.before.valid_mask==127&&p.after.valid_mask==127);
    assert(atomic_load(&fake_probe_calls)==2&&view().state.seed==5&&view().state.phase==ECHOY_IDLE);
    assert(!view().state.continuous);
    for(int i=0;i<100;i++)assert(echoy_device_probe_get(d,&p)==ESP_OK);
    assert(atomic_load(&fake_probe_calls)==2); /* Cached status has no I2C. */
    fake_sleep_ms(50);assert(view().state.phase==ECHOY_IDLE);
    stop(&d);puts("PASS explicit one-second reference, two readbacks, no auto replay and retained rhythm");
}
static void probe_cancel(void)
{
    echoy_device_handle_t d;start(&d);
    block_writer();
    assert(echoy_device_probe_request(d,true)==ESP_OK);
    assert(action(ECHOY_ACT_PAUSE,0));
    fake_hold_write(false);
    probe_until(d,ECHOY_PROBE_CANCELLED);
    assert(atomic_load(&fake_probe_calls)==0);
    assert(echoy_device_probe_request(d,true)==ESP_OK);probe_until(d,ECHOY_PROBE_PLAYING);
    assert(action(ECHOY_ACT_PAUSE,0));
    echoy_device_diagnostics_t p=probe_until(d,ECHOY_PROBE_CANCELLED);
    assert(p.frames_accepted<24000);assert(atomic_load(&fake_probe_calls)==1);
    assert(echoy_device_probe_request(d,true)==ESP_OK);probe_until(d,ECHOY_PROBE_PLAYING);
    assert(echoy_device_probe_request(d,false)==ESP_OK);probe_until(d,ECHOY_PROBE_CANCELLED);
    stop(&d);puts("PASS UI and explicit stop cancel diagnostic output");
}
static void probe_failure(void)
{
    echoy_device_handle_t d;start(&d);atomic_store(&fake_probe_failure,1);
    assert(echoy_device_probe_request(d,true)==ESP_OK);
    echoy_device_diagnostics_t p=probe_until(d,ECHOY_PROBE_FAILED);
    assert(p.error==ESP_ERR_TIMEOUT&&p.before.valid_mask==15&&p.frames_accepted==0);
    until(ECHOY_AUDIO_FAULT,ECHOY_IDLE,0);stop(&d);puts("PASS readback error is exposed and output stops");
}
static void probe_partial(void)
{
    echoy_device_handle_t d;start(&d);
    assert(echoy_device_probe_request(d,true)==ESP_OK);probe_until(d,ECHOY_PROBE_PLAYING);
    atomic_store(&fake_partial_next,1);
    echoy_device_diagnostics_t p=probe_until(d,ECHOY_PROBE_FAILED);
    assert(p.error==ESP_ERR_TIMEOUT&&p.frames_accepted<24000);
    until(ECHOY_AUDIO_FAULT,ECHOY_IDLE,0);stop(&d);puts("PASS partial reference write faults without replay");
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "cache"))
        unknown_and_cache();
    else if (!strcmp(argv[1], "fifo"))
        fifo_cancel();
    else if (!strcmp(argv[1], "limiter_pause"))
        pause_clears_limiter_tail();
    else if (!strcmp(argv[1], "calibration_failure"))
        calibration_failure();
    else if (!strcmp(argv[1], "overflow"))
        overflow();
    else if (!strcmp(argv[1], "partial"))
        partial_recovery();
    else if (!strcmp(argv[1], "battery"))
        empty_battery_latch();
    else if (!strcmp(argv[1], "startup"))
        startup_timeout();
    else if (!strcmp(argv[1], "prepare"))
        prepare_timeout();
    else if (!strcmp(argv[1], "task_failure"))
        task_failure();
    else if (!strcmp(argv[1], "health_stop"))
        health_stop_retry();
    else if (!strcmp(argv[1], "close_failure"))
        close_failure();
    else if (!strcmp(argv[1], "loading_cache"))
        loading_cache_contract();
    else if (!strcmp(argv[1], "open_failure"))
        open_failure_recovery();
    else if (!strcmp(argv[1], "flush_failure"))
        flush_failure();
    else if (!strcmp(argv[1], "deadline"))
        missed_deadline();
    else if (!strcmp(argv[1], "acceptance_gate"))
        acceptance_gate();
    else if (!strcmp(argv[1], "stop_unactivated"))
        stop_before_activation();
    else if (!strcmp(argv[1], "probe")) probe_complete();
    else if (!strcmp(argv[1], "probe_cancel")) probe_cancel();
    else if (!strcmp(argv[1], "probe_failure")) probe_failure();
    else if (!strcmp(argv[1], "probe_partial")) probe_partial();
    else
        return 2;
    return 0;
}
