#include "echoy_band.h"
#include "board_audio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "fakes.h"
#include "platform_device.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Capture only deadline diagnostics; normal fake logging remains silent. */
static char deadline_lines[4][640];
static atomic_uint deadline_line_count;
static atomic_int fake_write_delay_ms;
static atomic_bool fake_write_delay_entered;
static atomic_llong observed_write_delay_us;
static _Thread_local bool slow_cache_reader;
static atomic_bool reader_holds_cache, release_cache_reader;
static atomic_bool hold_measure_completion, measure_completion_blocked;
bool __real_echoy_band_action(echoy_band_t *, echoy_tile_id_t, echoy_action_t, double, int64_t);
bool __wrap_echoy_band_action(echoy_band_t *band, echoy_tile_id_t tile, echoy_action_t action,
                              double value, int64_t at)
{
    if (action == ECHOY_ACT_PAUSE && atomic_load(&fake_analysis_calls) &&
        atomic_load(&hold_measure_completion)) {
        atomic_store(&measure_completion_blocked, true);
        while (atomic_load(&hold_measure_completion)) fake_sleep_ms(1);
    }
    return __real_echoy_band_action(band, tile, action, value, at);
}
BaseType_t __real_xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t __wrap_xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout)
{
    BaseType_t result=__real_xSemaphoreTake(mutex,timeout);
    if (result==pdTRUE && slow_cache_reader) {
        assert(timeout==portMAX_DELAY);
        atomic_store(&reader_holds_cache,true);
        while (!atomic_load(&release_cache_reader)) fake_sleep_ms(1);
    }
    return result;
}
void __wrap_fake_log(const char *tag, const char *format, ...)
{
    (void)tag;
    char line[640];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (strncmp(line, "ECHOY_DEADLINE ", 15)) return;
    assert(atomic_load(&fake_close_calls) > 0); /* Never format/log while output is active. */
    unsigned index = atomic_load(&deadline_line_count);
    assert(index < 4);
    strcpy(deadline_lines[index], line);
    atomic_store(&deadline_line_count, index + 1);
}
esp_err_t __real_board_audio_write(board_audio_t *, const int16_t *, size_t, size_t *);
esp_err_t __wrap_board_audio_write(board_audio_t *audio, const int16_t *pcm, size_t frames,
                                  size_t *written)
{
    int delay = atomic_exchange(&fake_write_delay_ms, 0);
    int64_t before = esp_timer_get_time();
    if (delay) {
        atomic_store(&fake_write_delay_entered, true);
        fake_sleep_ms((unsigned)delay);
    }
    esp_err_t result = __real_board_audio_write(audio, pcm, frames, written);
    if (delay) atomic_store(&observed_write_delay_us, esp_timer_get_time() - before);
    return result;
}
static long long trace_field(const char *line, const char *name)
{
    char key[48];
    snprintf(key, sizeof(key), "%s=", name);
    const char *at = strstr(line, key);
    assert(at);
    return strtoll(at + strlen(key), NULL, 10);
}

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
struct cache_reader_args { echoy_device_handle_t device; bool link; };
static void *hold_cached_read(void *opaque)
{
    struct cache_reader_args *args=opaque;
    slow_cache_reader=true;
    if (args->link) {
        echoy_link_view_t cached;
        assert(echoy_device_link_read(args->device,&cached));
        assert(cached.current.source_count);
    } else {
        echoy_control_view_t cached=view();
        assert(cached.state.seed==5 && cached.state.continuous);
    }
    slow_cache_reader=false;
    return NULL;
}
static void cached_reader_cannot_block_audio(bool link)
{
    echoy_device_handle_t d;
    start(&d);
    assert(action(ECHOY_ACT_EXAMPLE,0));
    until(ECHOY_NORMAL,ECHOY_PREVIEW,5);
    assert(action(ECHOY_ACT_PAUSE,0));
    until(ECHOY_NORMAL,ECHOY_IDLE,5);
    assert(action(ECHOY_ACT_TRANSPORT,0));
    until(ECHOY_NORMAL,ECHOY_PLAYING,5);
    fake_sleep_ms(100); /* Include a full 50 ms link-cache publication. */
    struct cache_reader_args args={d,link};
    pthread_t reader;
    assert(!pthread_create(&reader,NULL,hold_cached_read,&args));
    int64_t end=esp_timer_get_time()+1000000;
    while (!atomic_load(&reader_holds_cache)) {
        assert(esp_timer_get_time()<end);
        fake_sleep_ms(1);
    }
    int writes=atomic_load(&fake_write_calls), closes=atomic_load(&fake_close_calls);
    fake_sleep_ms(80);
    /* The old blocking publisher stalls output here for longer than its lead. */
    assert(atomic_load(&fake_write_calls)>=writes+4);
    assert(atomic_load(&fake_close_calls)==closes);
    int flushes=atomic_load(&fake_flush_calls);
    assert(action(ECHOY_ACT_PAUSE,0));
    end=esp_timer_get_time()+100000;
    while (atomic_load(&fake_flush_calls)==flushes) {
        assert(esp_timer_get_time()<end);
        fake_sleep_ms(1);
    }
    fake_sleep_ms(30);
    assert(atomic_load(&fake_nonzero_since_flush)==0);
    assert(atomic_load(&fake_close_calls)==closes);
    atomic_store(&release_cache_reader,true);
    assert(!pthread_join(reader,NULL));
    until(ECHOY_NORMAL,ECHOY_IDLE,5);
    echoy_link_view_t latest;
    end=esp_timer_get_time()+100000;
    do {
        assert(echoy_device_link_read(d,&latest));
        if (!latest.looping) break;
        fake_sleep_ms(1);
    } while (esp_timer_get_time()<end);
    assert(!latest.looping && latest.current.source_count);
    assert(atomic_load(&deadline_line_count)==0);
    stop(&d);
    puts("PASS delayed cache reader cannot stop PCM or Pause; latest state publishes after release");
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
        assert(v.memories[role] == 1);
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

static void deadline_trace_is_frozen_and_truthful(void)
{
    echoy_device_handle_t d;
    start(&d);
    int64_t end = esp_timer_get_time() + 2000000;
    while (atomic_load(&fake_write_calls) < 24) {
        assert(esp_timer_get_time() < end);
        fake_sleep_ms(1);
    }
    assert(!atomic_load(&deadline_line_count)); /* No per-block diagnostic logging. */
    atomic_store(&fake_write_delay_ms, 80);
    while (!atomic_load(&fake_write_delay_entered)) {
        assert(esp_timer_get_time() < end);
        fake_sleep_ms(1);
    }
    board_link_update_t *update = calloc(1, sizeof(*update));
    assert(update);
    update->attached = true;
    update->connection_generation = 1;
    update->session = 17;
    update->transaction = 123456;
    update->command = ECHOY_LINK_NONE;
    assert(echoy_device_link_receive(d, update));
    free(update);
    until(ECHOY_AUDIO_FAULT, ECHOY_IDLE, 0);
    int writes = atomic_load(&fake_write_calls);
    fake_sleep_ms(40);
    assert(atomic_load(&fake_write_calls) == writes);
    assert(atomic_load(&deadline_line_count) == 4);
    stop(&d);
    const char *current = deadline_lines[0], *previous = deadline_lines[1];
    assert(trace_field(current, "age") == 0 && trace_field(current, "seq") > 16);
    long long checked = trace_field(current, "now"), expected = trace_field(current, "at");
    assert(checked > expected && trace_field(current, "late") == checked - expected);
    assert(trace_field(current, "tx") == 123456 && trace_field(current, "cmd") == ECHOY_LINK_NONE);
    assert(trace_field(current, "render") == -1 && trace_field(current, "write") == -1);
    const char *stages[] = {"health", "queue", "link", "commands", "finish", "tick", "take", "publish"};
    long long before = 0;
    for (unsigned i = 0; i < sizeof(stages) / sizeof(stages[0]); ++i) {
        long long value = trace_field(current, stages[i]);
        assert(value >= before); before = value;
    }
    assert(trace_field(current, "lv") >= 0 && trace_field(current, "wait") >= 0 &&
           trace_field(current, "copy") >= 0);
    long long write_us = trace_field(previous, "write") - trace_field(previous, "render");
    long long injected = atomic_load(&observed_write_delay_us);
    assert(injected >= 80000 && write_us >= injected && write_us - injected < 10000);
    for (unsigned i = 0; i < 4; ++i) {
        assert(trace_field(deadline_lines[i], "age") == i);
        assert(trace_field(deadline_lines[i], "seq") == trace_field(current, "seq") - i);
        puts(deadline_lines[i]);
    }
    puts("PASS frozen deadline trace names the delayed write, pending link metadata and exact lateness");
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
static void linked_snapshot_queue(void)
{
    echoy_device_handle_t d;
    start(&d);
    board_link_update_t *update = calloc(1, sizeof(*update));
    assert(update);
    update->attached = update->following = update->valid = update->clock_started = true;
    update->clock_origin_us = esp_timer_get_time() + 150000;
    update->connection_generation=1; update->session=123; update->transaction=7;
    update->command=ECHOY_LINK_OFFER;
    assert(echoy_compose_mask(0x1128,ECHOY_VOICE,2,.5,&update->plan));
    block_writer();
    assert(echoy_device_link_receive(d,update));
    memset(&update->plan,0,sizeof(update->plan)); /* admission must copy the complete snapshot */
    echoy_link_view_t link;
    assert(echoy_device_link_read(d,&link));
    fake_hold_write(false);
    int64_t end=esp_timer_get_time()+2000000;
    do {
        assert(echoy_device_link_read(d,&link));
        if (link.offer_ready && link.offered_transaction==7) break;
        fake_sleep_ms(1);
    } while (esp_timer_get_time()<end);
    assert(link.offer_ready && link.offered_transaction==7);
    assert(!view().plan.count); /* prepare is not publication */
    update->command=ECHOY_LINK_ARM;
    update->revision=link.offered_revision;
    update->commit_at_us=esp_timer_get_time()+400000;
    assert(echoy_device_link_receive(d,update));
    do {
        assert(echoy_device_link_read(d,&link));
        if (link.armed_transaction==7) break;
        fake_sleep_ms(1);
    } while (esp_timer_get_time()<end);
    assert(link.armed_transaction==7);
    update->command=ECHOY_LINK_COMMIT;
    assert(echoy_device_link_receive(d,update));
    echoy_control_view_t cached;
    end=esp_timer_get_time()+2000000;
    do {
        cached=view();
        if(cached.source==ECHOY_TILE_B && cached.plan.count) break;
        fake_sleep_ms(1);
    } while(esp_timer_get_time()<end);
    assert(cached.state.following && cached.source==ECHOY_TILE_B && cached.plan.count);
    assert(cached.state.phase==ECHOY_IDLE && atomic_load(&fake_nonzero_blocks)==0);
    size_t count=cached.plan.count;
    memset(update,0,sizeof(*update));
    update->connection_generation=2;
    assert(echoy_device_link_receive(d,update));
    end=esp_timer_get_time()+2000000;
    do {cached=view();if(!cached.state.following && cached.source==ECHOY_TILE_NONE) break;fake_sleep_ms(1);}
    while(esp_timer_get_time()<end);
    assert(!cached.state.following && cached.source==ECHOY_TILE_NONE && cached.plan.count==count);
    assert(cached.state.phase==ECHOY_IDLE && atomic_load(&fake_nonzero_blocks)==0);
    free(update);
    stop(&d);
    puts("PASS network snapshots are copied off audio, attach stays silent, detach retains phrase");
}
static void image_storage_uses_psram(void)
{
    echoy_device_handle_t device = NULL;
    assert(echoy_device_start(&device, &p) == ESP_OK);
    assert(p.ui_storage_alloc);
    unsigned before = atomic_load(&fake_image_alloc_calls);
    void *image = p.ui_storage_alloc(p.ctx, 1024);
    assert(image);
    memset(image, 0x5a, 1024);
    free(image); /* TAKE's default release uses free(), including on device. */
    assert(atomic_load(&fake_image_alloc_calls) == before + 1);
    assert(atomic_load(&fake_image_alloc_caps) == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    atomic_store(&fake_image_alloc_failure, 1);
    assert(!p.ui_storage_alloc(p.ctx, 1024));
    assert(atomic_load(&fake_image_alloc_calls) == before + 2);
    assert(atomic_load(&fake_image_alloc_caps) == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    atomic_store(&fake_image_alloc_failure, 0);
    stop(&device);
    puts("PASS retained image storage requires free-compatible PSRAM without internal fallback");
}
static echoy_output_measurement_status_t measure_until(echoy_device_handle_t d,
    echoy_output_measurement_phase_t phase)
{
    echoy_output_measurement_status_t s;
    int64_t end = esp_timer_get_time() + 4000000;
    do {
        assert(echoy_device_measure_get(d, &s) == ESP_OK);
        if (s.phase == phase) return s;
        fake_sleep_ms(1);
    } while (esp_timer_get_time() < end);
    fprintf(stderr, "measurement phase %d expected %d err %d frames %u\n",
            s.phase, phase, s.error_code, s.frames_captured);
    abort();
}
static void measurement_complete(void)
{
    echoy_device_handle_t d; start(&d);
    echoy_output_measurement_status_t s;
    assert(echoy_device_measure_get(d, &s) == ESP_OK);
    assert(s.phase == ECHOY_OUTPUT_MEASUREMENT_IDLE && s.runs_remaining == 2);
    assert(atomic_load(&fake_measure_open_calls) == 0);
    assert(echoy_device_measure_request(d, true, 17, &s) == ESP_ERR_INVALID_ARG);
    assert(echoy_device_measure_request(d, true, 36, &s) == ESP_ERR_INVALID_ARG);
    assert(echoy_device_measure_get(NULL, &s) == ESP_ERR_INVALID_ARG);
    assert(echoy_device_measure_get(d, NULL) == ESP_ERR_INVALID_ARG);
    assert(action(ECHOY_ACT_EXAMPLE, 0)); until(ECHOY_NORMAL, ECHOY_PREVIEW, 5);
    assert(action(ECHOY_ACT_PAUSE, 0)); until(ECHOY_NORMAL, ECHOY_IDLE, 5);
    assert(action(ECHOY_ACT_TRANSPORT, 0)); until(ECHOY_NORMAL, ECHOY_PLAYING, 5);
    assert(view().state.continuous);
    assert(echoy_device_measure_request(d, true, 18, &s) == ESP_OK);
    assert(s.generation == 1 && s.runs_started == 1 && s.runs_remaining == 1);
    assert(echoy_device_measure_request(d, true, 18, NULL) == ESP_ERR_INVALID_STATE);
    assert(echoy_device_probe_request(d, true) == ESP_ERR_INVALID_STATE);
    s = measure_until(d, ECHOY_OUTPUT_MEASUREMENT_COMPLETE);
    assert(s.result_ready && s.frames_captured == ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES &&
           s.frames_written == ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES);
    assert(!atomic_load(&fake_measure_live) && atomic_load(&fake_analysis_calls) == 1);
    assert(fake_measure_pcm_cleared());
    assert(atomic_load(&fake_measure_gain) == 18);
    echoy_output_measurement_result_t *r = calloc(1, sizeof(*r)); assert(r);
    assert(echoy_device_measure_result(d, 2, r) == ESP_ERR_INVALID_STATE);
    assert(echoy_device_measure_result(d, 1, r) == ESP_OK);
    assert(r->tones_analyzed == 15);
    assert(view().state.seed == 5 && view().state.phase == ECHOY_IDLE && !view().state.continuous);
    int reads = atomic_load(&fake_measure_read_calls);
    for (int i=0; i<100; ++i) assert(echoy_device_measure_get(d, &s) == ESP_OK);
    assert(atomic_load(&fake_measure_read_calls) == reads);
    assert(echoy_device_measure_request(d, true, 0, &s) == ESP_OK);
    assert(s.generation == 2 && !s.runs_remaining);
    assert(echoy_device_measure_result(d, 1, r) == ESP_ERR_INVALID_STATE);
    assert(echoy_device_measure_request(d, false, 0, NULL) == ESP_OK);
    measure_until(d, ECHOY_OUTPUT_MEASUREMENT_CANCELLED);
    assert(echoy_device_measure_request(d, true, 18, NULL) == ESP_ERR_INVALID_STATE);
    free(r); stop(&d);
    start(&d); /* Runtime recreation during one boot cannot replenish the two-run allowance. */
    assert(echoy_device_measure_request(d, true, 18, NULL) == ESP_ERR_INVALID_STATE);
    stop(&d);
    puts("PASS bounded explicit measurement keeps rhythm, copies result and never resumes music");
}
static void measurement_cancel(void)
{
    echoy_device_handle_t d; start(&d); block_writer();
    assert(echoy_device_measure_request(d, true, 18, NULL) == ESP_OK);
    assert(action(ECHOY_ACT_PAUSE, 0));
    fake_hold_write(false);
    measure_until(d, ECHOY_OUTPUT_MEASUREMENT_CANCELLED);
    assert(atomic_load(&fake_measure_open_calls) == 0);
    assert(echoy_device_measure_request(d, true, 24, NULL) == ESP_OK);
    measure_until(d, ECHOY_OUTPUT_MEASUREMENT_CAPTURING);
    assert(action(ECHOY_ACT_RECORD, 0));
    echoy_output_measurement_status_t s = measure_until(d, ECHOY_OUTPUT_MEASUREMENT_CANCELLED);
    assert(s.frames_captured < ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES && !s.result_ready);
    assert(!atomic_load(&fake_measure_live) && !atomic_load(&fake_analysis_calls));
    assert(view().state.phase == ECHOY_IDLE && !view().state.continuous);
    stop(&d); puts("PASS queued and active cancellation cannot start stale capture or recording");
}
static void measurement_fault(const char *which)
{
    echoy_device_handle_t d; start(&d);
    if (!strcmp(which, "open")) atomic_store(&fake_measure_open_failure, 1);
    if (!strcmp(which, "flush")) atomic_store(&fake_flush_failure, 1);
    assert(echoy_device_measure_request(d, true, 18, NULL) == ESP_OK);
    if (strcmp(which, "open") && strcmp(which, "flush")) {
        measure_until(d, ECHOY_OUTPUT_MEASUREMENT_CAPTURING);
        if (!strcmp(which, "read")) atomic_store(&fake_measure_read_failure, 1);
        if (!strcmp(which, "partial_read")) atomic_store(&fake_measure_partial, 1);
        if (!strcmp(which, "partial_read_ok")) atomic_store(&fake_measure_partial, 2);
        if (!strcmp(which, "partial_write")) atomic_store(&fake_partial_next, 1);
        if (!strcmp(which, "partial_write_ok")) atomic_store(&fake_partial_next, 2);
        if (!strcmp(which, "deadline")) atomic_store(&fake_measure_expire, 1);
        if (!strcmp(which, "close")) {
            atomic_store(&fake_close_failure, 1);
            assert(echoy_device_measure_request(d, false, 0, NULL) == ESP_OK);
        }
        if (!strcmp(which, "reopen")) {
            atomic_store(&fake_open_failure, 1);
            assert(echoy_device_measure_request(d, false, 0, NULL) == ESP_OK);
        }
    }
    echoy_output_measurement_status_t s = measure_until(d, ECHOY_OUTPUT_MEASUREMENT_FAILED);
    assert(s.error_code != ESP_OK && !s.result_ready && !atomic_load(&fake_measure_live));
    assert(fake_measure_pcm_cleared());
    until(ECHOY_AUDIO_FAULT, ECHOY_IDLE, 0);
    int writes = atomic_load(&fake_write_calls); fake_sleep_ms(30);
    assert(atomic_load(&fake_write_calls) == writes);
    if (!strcmp(which, "close")) {
        assert(echoy_device_stop(&d, 2000) == ESP_FAIL); assert(d);
        retained_failure = d; /* Hardware ownership failure deliberately retains all context. */
    } else stop(&d);
    puts("PASS measurement I/O/start/cleanup failure stops capture and exposes fault");
}
static void measurement_analysis_stop(void)
{
    echoy_device_handle_t d; start(&d);
    atomic_store(&fake_analysis_hold, 1);
    assert(echoy_device_measure_request(d, true, 18, NULL) == ESP_OK);
    measure_until(d, ECHOY_OUTPUT_MEASUREMENT_ANALYZING);
    int64_t end = esp_timer_get_time()+1000000;
    while (!atomic_load(&fake_analysis_blocked)) {assert(esp_timer_get_time()<end);fake_sleep_ms(1);}
    int writes=atomic_load(&fake_write_calls); fake_sleep_ms(30);
    assert(!atomic_load(&fake_measure_live) && atomic_load(&fake_write_calls)>writes);
    assert(echoy_device_stop(&d, 10) == ESP_ERR_TIMEOUT && d);
    atomic_store(&fake_analysis_hold, 0); stop(&d);
    puts("PASS analysis stays off audio owner; shutdown retains buffer until worker stops");
}
static void measurement_analysis_cancel(void)
{
    echoy_device_handle_t d; start(&d);
    atomic_store(&fake_analysis_hold, 1);
    assert(echoy_device_measure_request(d, true, 18, NULL) == ESP_OK);
    measure_until(d, ECHOY_OUTPUT_MEASUREMENT_ANALYZING);
    assert(echoy_device_measure_request(d, false, 0, NULL) == ESP_OK);
    assert(echoy_device_measure_request(d, true, 18, NULL) == ESP_ERR_INVALID_STATE);
    atomic_store(&fake_analysis_hold, 0);
    echoy_output_measurement_status_t s=measure_until(d, ECHOY_OUTPUT_MEASUREMENT_CANCELLED);
    assert(!s.result_ready && !atomic_load(&fake_measure_live));
    assert(echoy_device_measure_request(d, true, 12, NULL) == ESP_OK);
    measure_until(d, ECHOY_OUTPUT_MEASUREMENT_CAPTURING);
    assert(echoy_device_measure_request(d, false, 0, NULL) == ESP_OK);
    s=measure_until(d, ECHOY_OUTPUT_MEASUREMENT_CANCELLED);
    assert(s.generation==2 && !s.result_ready);
    stop(&d); puts("PASS cancelled analysis cannot publish stale results into a later run");
}
static void measurement_disabled(void)
{
    echoy_device_handle_t d; start(&d);
    echoy_output_measurement_status_t s;
    assert(echoy_device_measure_get(d, &s)==ESP_OK);
    assert(s.phase==ECHOY_OUTPUT_MEASUREMENT_DISABLED);
    assert(echoy_device_measure_request(d,true,18,NULL)==ESP_ERR_NOT_SUPPORTED);
    assert(!atomic_load(&fake_measure_open_calls));
    assert(!view().measurement_active && !view().measurement_microphone);
    stop(&d); puts("PASS ordinary builds never permit microphone measurement");
}
static void *hold_measure_read(void *opaque)
{
    echoy_output_measurement_status_t s;
    slow_cache_reader = true;
    assert(echoy_device_measure_get(opaque, &s) == ESP_OK);
    slow_cache_reader = false;
    return NULL;
}
static void measurement_reader(void)
{
    echoy_device_handle_t d; start(&d);
    assert(echoy_device_measure_request(d,true,6,NULL)==ESP_OK);
    measure_until(d,ECHOY_OUTPUT_MEASUREMENT_CAPTURING);
    pthread_t reader; assert(!pthread_create(&reader,NULL,hold_measure_read,d));
    int64_t end=esp_timer_get_time()+1000000;
    while(!atomic_load(&reader_holds_cache)){assert(esp_timer_get_time()<end);fake_sleep_ms(1);}
    int reads=atomic_load(&fake_measure_read_calls); fake_sleep_ms(30);
    assert(atomic_load(&fake_measure_read_calls)>reads);
    assert(echoy_device_measure_request(d,false,0,NULL)==ESP_OK);
    while(atomic_load(&fake_measure_live)){assert(esp_timer_get_time()<end);fake_sleep_ms(1);}
    reads=atomic_load(&fake_measure_read_calls);fake_sleep_ms(20);
    assert(atomic_load(&fake_measure_read_calls)==reads);
    atomic_store(&release_cache_reader,true); assert(!pthread_join(reader,NULL));
    measure_until(d,ECHOY_OUTPUT_MEASUREMENT_CANCELLED);
    assert(fake_measure_pcm_cleared()); stop(&d);
    puts("PASS slow measurement RPC reader cannot block capture or cancellation cleanup");
}
static void measurement_capture_stop(void)
{
    echoy_device_handle_t d; start(&d);
    assert(echoy_device_measure_request(d,true,30,NULL)==ESP_OK);
    measure_until(d,ECHOY_OUTPUT_MEASUREMENT_CAPTURING);
    stop(&d); assert(!atomic_load(&fake_measure_live));
    assert(!atomic_load(&fake_analysis_calls));
    puts("PASS shutdown closes active microphone before freeing capture storage");
}
static void measurement_allocation_failure(void)
{
    echoy_device_handle_t d=NULL;
    atomic_store(&fake_measure_alloc_failure,1);
    assert(echoy_device_start(&d,&p)==ESP_ERR_NO_MEM && !d);
    assert(!atomic_load(&fake_open_calls) && !atomic_load(&fake_measure_open_calls));
    puts("PASS missing PSRAM does not fall back to internal memory or start audio");
}
static void measurement_cancel_at_publication(void)
{
    echoy_device_handle_t d; start(&d);
    atomic_store(&hold_measure_completion, true);
    assert(echoy_device_measure_request(d,true,18,NULL)==ESP_OK);
    int64_t end=esp_timer_get_time()+3000000;
    while(!atomic_load(&measure_completion_blocked)){assert(esp_timer_get_time()<end);fake_sleep_ms(1);}
    assert(action(ECHOY_ACT_PAUSE,0));
    atomic_store(&hold_measure_completion,false);
    echoy_output_measurement_status_t s=measure_until(d,ECHOY_OUTPUT_MEASUREMENT_CANCELLED);
    assert(!s.result_ready && fake_measure_pcm_cleared());
    echoy_output_measurement_result_t *result=calloc(1,sizeof(*result));assert(result);
    assert(echoy_device_measure_result(d,s.generation,result)==ESP_ERR_INVALID_STATE);
    free(result);stop(&d);
    puts("PASS cancellation after analysis completion still prevents publishing a result");
}
static void measurement_retained_cleanup(bool health)
{
    echoy_device_handle_t d; start(&d);
    assert(echoy_device_measure_request(d,true,18,NULL)==ESP_OK);
    measure_until(d,ECHOY_OUTPUT_MEASUREMENT_CAPTURING);
    int64_t end=esp_timer_get_time()+1000000;
    unsigned tone_blocks = ECHOY_OUTPUT_MEASUREMENT_LEAD_FRAMES / 240u + 20u;
    while(atomic_load(&fake_measure_read_calls)<tone_blocks){assert(esp_timer_get_time()<end);fake_sleep_ms(1);}
    /* The first tone has been captured; do not inspect storage while RX owns it. */
    atomic_store(health ? &fake_health_stop_timeout : &fake_close_failure,1);
    assert(echoy_device_stop(&d,2000)==(health ? ESP_ERR_TIMEOUT : ESP_FAIL) && d);
    assert(!atomic_load(&fake_measure_live) && fake_measure_pcm_cleared());
    if(health) stop(&d); else retained_failure=d;
    puts("PASS failed shutdown retains lifecycle context but erases raw microphone samples");
}
static void measurement_overflow(void)
{
    echoy_device_handle_t d; start(&d); block_writer();
    assert(echoy_device_measure_request(d, true, 18, NULL) == ESP_OK);
    for (int i=0; i<40; ++i) action(ECHOY_ACT_VOLUME, .5);
    assert(echoy_device_measure_request(d, false, 0, NULL) == ESP_ERR_NO_MEM);
    fake_hold_write(false);
    measure_until(d, ECHOY_OUTPUT_MEASUREMENT_FAILED);
    until(ECHOY_AUDIO_FAULT, ECHOY_IDLE, 0);
    assert(!atomic_load(&fake_measure_open_calls));
    stop(&d); puts("PASS overflowing cancel invalidates the queued run and fails silent");
}
static void harmony_start(echoy_device_handle_t *d)
{
    start(d);
    assert(action(ECHOY_ACT_ROLE,ECHOY_HARMONY));
    assert(action(ECHOY_ACT_EXAMPLE,0));
    until(ECHOY_NORMAL,ECHOY_PREVIEW,5);
    assert(view().plan.role==ECHOY_HARMONY);
    assert(action(ECHOY_ACT_PAUSE,0));until(ECHOY_NORMAL,ECHOY_IDLE,5);
}
static void harmony_trial_disabled(void)
{
    echoy_device_handle_t d;harmony_start(&d);
    assert(!atomic_load(&fake_harmony_cut_calls));
    stop(&d);puts("PASS ordinary build leaves Harmony synthesis unchanged");
}
static void harmony_trial_worker_and_pause(void)
{
    echoy_device_handle_t d;harmony_start(&d);
    assert(atomic_load(&fake_harmony_cut_calls)==1);
    atomic_store(&fake_harmony_cut_hold,true);
    assert(action(ECHOY_ACT_PHRASE,2));
    int64_t end=esp_timer_get_time()+2000000;
    while(!atomic_load(&fake_harmony_cut_blocked)){assert(esp_timer_get_time()<end);fake_sleep_ms(1);}
    int writes=atomic_load(&fake_write_calls);fake_sleep_ms(40);
    assert(atomic_load(&fake_write_calls)>writes && view().plan.phrase==1);
    assert(view().state.phase==ECHOY_IDLE && !view().state.continuous);
    atomic_store(&fake_harmony_cut_hold,false);
    do {if(view().plan.phrase==2)break;fake_sleep_ms(1);}while(esp_timer_get_time()<end);
    assert(view().plan.phrase==2 && view().state.phase==ECHOY_IDLE && !view().state.continuous);
    assert(action(ECHOY_ACT_TRANSPORT,0));until(ECHOY_NORMAL,ECHOY_PLAYING,5);
    fake_sleep_ms(80);
    assert(action(ECHOY_ACT_PAUSE,0));until(ECHOY_NORMAL,ECHOY_IDLE,5);fake_sleep_ms(30);
    assert(!atomic_load(&fake_nonzero_since_flush));
    stop(&d);puts("PASS Harmony trial prepares off owner, remains paused and Pause clears PCM");
}
static void harmony_trial_failure(void)
{
    echoy_device_handle_t d;harmony_start(&d);
    unsigned calibrations=atomic_load(&fake_calibration_calls);
    atomic_store(&fake_harmony_cut_failure,1);
    assert(action(ECHOY_ACT_PHRASE,2));
    int64_t end=esp_timer_get_time()+2000000;
    while(!view().preparation_failed){assert(esp_timer_get_time()<end);fake_sleep_ms(1);}
    echoy_control_view_t v=view();
    assert(v.condition==ECHOY_NORMAL && v.plan.phrase==1 && v.plan.role==ECHOY_HARMONY);
    assert(!v.state.continuous && v.state.phase==ECHOY_IDLE);
    assert(atomic_load(&fake_calibration_calls)==calibrations);
    assert(action(ECHOY_ACT_PHRASE,3));
    do {if(view().plan.phrase==3)break;fake_sleep_ms(1);}while(esp_timer_get_time()<end);
    assert(view().plan.phrase==3 && !view().preparation_failed && !view().state.continuous);
    stop(&d);puts("PASS failed Harmony filter rejects whole job and retains previous valid sound");
}
static void harmony_trial_cancel_stale(void)
{
    echoy_device_handle_t d;harmony_start(&d);
    atomic_store(&fake_harmony_cut_hold,true);
    assert(action(ECHOY_ACT_PHRASE,2));
    int64_t end=esp_timer_get_time()+2000000;
    while(!atomic_load(&fake_harmony_cut_blocked)){assert(esp_timer_get_time()<end);fake_sleep_ms(1);}
    assert(action(ECHOY_ACT_PHRASE,3)); /* Supersede the immutable phrase-2 job. */
    assert(action(ECHOY_ACT_PAUSE,0));
    fake_sleep_ms(20);
    atomic_store(&fake_harmony_cut_hold,false);
    while(atomic_load(&fake_harmony_cut_blocked)){assert(esp_timer_get_time()<end);fake_sleep_ms(1);}
    do {if(view().plan.phrase==3)break;fake_sleep_ms(1);}while(esp_timer_get_time()<end);
    echoy_control_view_t v=view();
    assert(v.state.phase==ECHOY_IDLE && !v.state.continuous && v.plan.phrase==3);
    assert(atomic_load(&fake_harmony_cut_calls)>=3);
    assert(!atomic_load(&fake_nonzero_since_flush));
    stop(&d);puts("PASS cancelled stale Harmony worker cannot publish or replay its bank");
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "harmony_disabled")) harmony_trial_disabled();
    else if (!strcmp(argv[1], "harmony_worker")) harmony_trial_worker_and_pause();
    else if (!strcmp(argv[1], "harmony_failure")) harmony_trial_failure();
    else if (!strcmp(argv[1], "harmony_stale")) harmony_trial_cancel_stale();
    else if (!strcmp(argv[1], "measure_complete")) measurement_complete();
    else if (!strcmp(argv[1], "measure_cancel")) measurement_cancel();
    else if (!strcmp(argv[1], "measure_analysis_stop")) measurement_analysis_stop();
    else if (!strcmp(argv[1], "measure_analysis_cancel")) measurement_analysis_cancel();
    else if (!strcmp(argv[1], "measure_disabled")) measurement_disabled();
    else if (!strcmp(argv[1], "measure_reader")) measurement_reader();
    else if (!strcmp(argv[1], "measure_capture_stop")) measurement_capture_stop();
    else if (!strcmp(argv[1], "measure_alloc_failure")) measurement_allocation_failure();
    else if (!strcmp(argv[1], "measure_publish_cancel")) measurement_cancel_at_publication();
    else if (!strcmp(argv[1], "measure_stop_close")) measurement_retained_cleanup(false);
    else if (!strcmp(argv[1], "measure_stop_health")) measurement_retained_cleanup(true);
    else if (!strcmp(argv[1], "measure_overflow")) measurement_overflow();
    else if (!strncmp(argv[1], "measure_fault_", 14)) measurement_fault(argv[1]+14);
    else if (!strcmp(argv[1], "image_storage"))
        image_storage_uses_psram();
    else if (!strcmp(argv[1], "link"))
        linked_snapshot_queue();
    else if (!strcmp(argv[1], "cache_contention_control"))
        cached_reader_cannot_block_audio(false);
    else if (!strcmp(argv[1], "cache_contention_link"))
        cached_reader_cannot_block_audio(true);
    else if (!strcmp(argv[1], "cache"))
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
    else if (!strcmp(argv[1], "deadline_trace"))
        deadline_trace_is_frozen_and_truthful();
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
