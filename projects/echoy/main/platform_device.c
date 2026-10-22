// SPDX-License-Identifier: Apache-2.0
#include "platform_device.h"
#include "board_audio.h"
#include "board_health.h"
#include "echoy_audio_probe.h"
#include "echoy_band.h"
#include "echoy_device_pcm.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if CONFIG_ECHOY_HARMONY_BASS_TRIAL && CONFIG_ECHOY_OFFICE_VOICING_TRIAL
#error "Echoy audio trials are mutually exclusive"
#endif

#ifndef CONFIG_ECHOY_PHRASE_PICKUP
#define CONFIG_ECHOY_PHRASE_PICKUP 0
#endif
#define BLOCK_FRAMES 240u
#define QUEUE_US                                                                                   \
    ((int64_t)BOARD_AUDIO_DMA_FRAMES * BOARD_AUDIO_DMA_DESCRIPTORS * 1000000 /                     \
     BOARD_AUDIO_SAMPLE_RATE)
#define OWNER_DONE BIT0
#define PREPARE_DONE BIT1
#define STARTED BIT2
#define COMMAND_COUNT 32
#define DIAGNOSTIC_BYTES 512
#define OWNER_TRACE_ROWS 16u
#define AUDIO_OWNER_PRIORITY 6
#ifdef ESP_PLATFORM
_Static_assert(AUDIO_OWNER_PRIORITY > CONFIG_ESP_IRIS_TASK_PRIORITY,
               "PCM owner must preempt Iris worker and service tasks");
_Static_assert(AUDIO_OWNER_PRIORITY > CONFIG_ESP_GSP_RENDER_TASK_PRIORITY,
               "PCM owner must preempt rendering");
_Static_assert(AUDIO_OWNER_PRIORITY < configMAX_PRIORITIES,
               "PCM owner priority must fit the RTOS configuration");
#endif
static const char *TAG = "echoy_device";
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
/* A stopped/recreated runtime must not reset the authorization's boot budget. */
static atomic_uint measure_boot_runs, measure_boot_sequence;
#endif

_Static_assert(BOARD_AUDIO_SAMPLE_RATE == 24000, "PCM limiter requires 24 kHz");

typedef struct {
    unsigned probe_command; /* 0 UI, 1 reference tone, 2 cancel diagnostic. */
    unsigned probe_generation;
    unsigned measure_command, measure_generation, measure_gain;
    bool cancels_measurement;
    echoy_action_t action;
    double value;
    int64_t at;
} command_t;
typedef struct {
    echoy_band_job_t *job;
    int64_t elapsed_us;
    bool ok, calibration_failed;
} prepared_t;
typedef struct {
    float floating[BLOCK_FRAMES];
    float pickup[BLOCK_FRAMES];
    uint8_t pickup_gate[BLOCK_FRAMES];
    int16_t pcm[BLOCK_FRAMES];
} pcm_block_t;
enum owner_trace_phase {
    TRACE_HEALTH, TRACE_QUEUE, TRACE_LINK, TRACE_COMMANDS, TRACE_FINISH, TRACE_TICK,
    TRACE_TAKE, TRACE_PUBLISH, TRACE_RENDER, TRACE_WRITE, TRACE_PHASE_COUNT
};
enum {
    TRACE_GOT_LINK = 1u, TRACE_GOT_JOB = 2u, TRACE_TOOK_JOB = 4u,
    TRACE_LINK_VIEW = 8u, TRACE_PROBE = 16u, TRACE_FLUSH = 32u,
    TRACE_CACHE_BUSY = 64u
};
typedef struct {
    int64_t entry_us, expected_us, checked_us;
    /* Completion offsets from entry, in microseconds; -1 means not executed. */
    int32_t completed_us[TRACE_PHASE_COUNT];
    uint32_t link_view_us, mutex_wait_us, cache_copy_us;
    uint32_t sequence, transaction, flags;
    unsigned link_command;
} owner_trace_t;
struct echoy_device {
    QueueHandle_t commands, startup, jobs, prepared, diagnostics;
    QueueHandle_t link_updates;
    board_link_update_t *link_staging;
    SemaphoreHandle_t view_lock;
    EventGroupHandle_t done;
    TaskHandle_t owner_task, prepare_task;
    board_health_handle_t health;
    echoy_band_t *band; /* Exclusively owned by audio/control after startup handoff. */
    echoy_control_view_t cache, staging;
    echoy_link_view_t link_cache, link_view_staging;
    int64_t next_link_publish_us;
    /* Owner-only numeric history stays in this PSRAM context, not its stack. */
    owner_trace_t trace[OWNER_TRACE_ROWS];
    owner_trace_t *trace_active;
    unsigned trace_next, trace_count;
    uint32_t trace_sequence;
    bool trace_frozen, trace_pending;
    pcm_block_t *block;
    atomic_bool stopping, queue_fault, accepting, owner_exited, prepare_exited, activated;
    bool audio_fault, fatal, preparation_in_flight, battery_empty;
    echoy_condition_t condition;
    int64_t origin_us, next_log_us, last_prepare_us, max_render_us;
    int64_t max_mix_us, max_convert_us;
    int audio_core;
    uint64_t accepted_frames;
    echoy_device_pcm_t pcm_processor;
    echoy_pickup_pcm_t pickup_processor;
    uint64_t pcm_clipped, pcm_limited;
    unsigned deadline_misses, rejected_actions, queue_overflows, cache_busy;
    uint64_t density_rejected_request;
    esp_err_t shutdown_error;
    board_audio_stats_t last_audio_stats;
    atomic_bool probe_busy;
    atomic_uint probe_sequence;
    bool probe_pending, probe_active;
    size_t probe_position;
    echoy_device_diagnostics_t probe, probe_cache;
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    atomic_bool measure_busy, measure_job_pending, measure_job_done;
    atomic_uint measure_sequence, measure_cancel_sequence;
    bool measure_pending, measure_io, measure_ending, measure_job_in_flight;
    bool measure_analyze, measure_analysis_ok, measure_publish_result;
    unsigned measure_job_generation;
    int64_t measure_started_us, measure_deadline_us;
    echoy_output_measurement_phase_t measure_terminal;
    echoy_output_measurement_status_t measure, measure_cache;
    int16_t *measure_pcm;
    echoy_output_measurement_result_t *measure_work, *measure_result_cache;
#endif
};

static int64_t now_us(void *ctx)
{
    (void)ctx;
    return esp_timer_get_time();
}
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
static void measure_cancel_generation(struct echoy_device *d, unsigned generation)
{
    unsigned previous = atomic_load(&d->measure_cancel_sequence);
    while (previous < generation &&
           !atomic_compare_exchange_weak(&d->measure_cancel_sequence, &previous, generation)) {}
}
#endif
static int32_t trace_elapsed(int64_t now, int64_t before)
{
    int64_t elapsed = now - before;
    return elapsed < 0 ? 0 : elapsed > INT32_MAX ? INT32_MAX : (int32_t)elapsed;
}
static void trace_begin(struct echoy_device *d)
{
    d->trace_active = NULL;
    if (d->trace_frozen) return;
    owner_trace_t *row = &d->trace[d->trace_next];
    memset(row, 0, sizeof(*row));
    for (unsigned i = 0; i < TRACE_PHASE_COUNT; ++i) row->completed_us[i] = -1;
    row->entry_us = now_us(d);
    row->sequence = ++d->trace_sequence;
    d->trace_next = (d->trace_next + 1) % OWNER_TRACE_ROWS;
    if (d->trace_count < OWNER_TRACE_ROWS) ++d->trace_count;
    d->trace_active = row;
}
static void trace_mark(struct echoy_device *d, enum owner_trace_phase phase)
{
    if (d->trace_active)
        d->trace_active->completed_us[phase] = trace_elapsed(now_us(d), d->trace_active->entry_us);
}
static void trace_freeze(struct echoy_device *d, int64_t expected, int64_t checked)
{
    if (!d->trace_active) trace_begin(d);
    if (d->trace_active) {
        d->trace_active->expected_us = expected;
        d->trace_active->checked_us = checked;
        d->trace_frozen = d->trace_pending = true;
        d->trace_active = NULL;
    }
}
/* Only called after closing audio. The current and three previous rounds are
 * sufficient to distinguish owner work, lock contention and a delayed write. */
static void trace_log_deadline(struct echoy_device *d, esp_err_t error)
{
    for (unsigned age = 0; age < 4; ++age) {
        if (age >= d->trace_count) {
            ESP_LOGE(TAG, "ECHOY_DEADLINE age=%u unavailable=1", age);
            continue;
        }
        unsigned index = (d->trace_next + OWNER_TRACE_ROWS - 1 - age) % OWNER_TRACE_ROWS;
        const owner_trace_t *r = &d->trace[index];
        char line[512];
        int used = snprintf(line, sizeof(line),
            "ECHOY_DEADLINE age=%u seq=%u entry=%lld at=%lld now=%lld late=%lld "
            "health=%ld queue=%ld link=%ld commands=%ld finish=%ld tick=%ld take=%ld publish=%ld "
            "render=%ld write=%ld lv=%u wait=%u copy=%u cmd=%u tx=%u flags=%u",
            age, (unsigned)r->sequence, (long long)r->entry_us, (long long)r->expected_us,
            (long long)r->checked_us, (long long)(r->checked_us - r->expected_us),
            (long)r->completed_us[TRACE_HEALTH], (long)r->completed_us[TRACE_QUEUE],
            (long)r->completed_us[TRACE_LINK],
            (long)r->completed_us[TRACE_COMMANDS], (long)r->completed_us[TRACE_FINISH],
            (long)r->completed_us[TRACE_TICK], (long)r->completed_us[TRACE_TAKE],
            (long)r->completed_us[TRACE_PUBLISH], (long)r->completed_us[TRACE_RENDER],
            (long)r->completed_us[TRACE_WRITE], (unsigned)r->link_view_us,
            (unsigned)r->mutex_wait_us, (unsigned)r->cache_copy_us, r->link_command,
            (unsigned)r->transaction, (unsigned)r->flags);
        if (!age && used > 0 && (size_t)used < sizeof(line))
            snprintf(line + used, sizeof(line) - (size_t)used,
                " err=%s accepted=%llu render_max=%lld write_max=%u",
                esp_err_to_name(error), (unsigned long long)d->last_audio_stats.frames_written,
                (long long)d->max_render_us, (unsigned)d->last_audio_stats.max_write_us);
        ESP_LOGE(TAG, "%s", line);
    }
    d->trace_pending = false;
}
static TickType_t ticks(uint32_t ms)
{
    TickType_t value = pdMS_TO_TICKS(ms);
    return value ? value : 1;
}
static bool read_control(void *ctx, echoy_control_view_t *out)
{
    struct echoy_device *d = ctx;
    if (!d || !out) {
        return false;
    }
    /* The writer only holds this mutex for memcpy, never band work or I/O. */
    xSemaphoreTake(d->view_lock, portMAX_DELAY);
    *out = d->cache;
    xSemaphoreGive(d->view_lock);
    return true;
}
static void *ui_storage_alloc(void *ctx, size_t size)
{
    (void)ctx;
    /* View cache and image sources are UI-only storage. Keep them out of the
     * internal heap needed by audio/radio, even when that heap has room. */
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static bool perform_action(void *ctx, echoy_action_t action, double value, int64_t at)
{
    struct echoy_device *d = ctx;
    if (!d || !isfinite(value) || at < 0 || !atomic_load(&d->accepting) ||
        atomic_load(&d->stopping)) {
        return false;
    }
    /* Hardware presence and health cannot be overridden by simulator controls. */
    if (action == ECHOY_ACT_PRESENT) {
        return false;
    }
    command_t command = {.action = action, .value = value, .at = at};
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    if (atomic_load(&d->measure_busy)) {
        command.cancels_measurement = true;
        command.measure_generation = atomic_load(&d->measure_sequence);
        measure_cancel_generation(d, command.measure_generation);
    }
#endif
    if (xQueueSend(d->commands, &command, 0) != pdTRUE) {
        /* Do not lose Cancel and later execute an older Record. Fail silent,
         * discard all queued actions, and require explicit recovery. */
        atomic_store(&d->queue_fault, true);
        return false;
    }
    return true;
}
bool echoy_device_link_read(void *ctx, echoy_link_view_t *out)
{
    struct echoy_device *d = ctx;
    if (!d || !out || atomic_load(&d->stopping)) return false;
    xSemaphoreTake(d->view_lock, portMAX_DELAY);
    *out=d->link_cache;
    xSemaphoreGive(d->view_lock);
    return true;
}
bool echoy_device_link_receive(void *ctx, const board_link_update_t *update)
{
    struct echoy_device *d = ctx;
    return d && update && !atomic_load(&d->stopping) &&
           xQueueOverwrite(d->link_updates, update) == pdTRUE;
}
static void process_link(struct echoy_device *d)
{
    if (!d->band) return;
    bool received = xQueueReceive(d->link_updates, d->link_staging, 0) == pdTRUE;
    trace_mark(d, TRACE_QUEUE);
    if (!received) return;
    const board_link_update_t *u = d->link_staging;
    if (d->trace_active) {
        d->trace_active->flags |= TRACE_GOT_LINK;
        d->trace_active->link_command = (unsigned)u->command;
        d->trace_active->transaction = u->transaction;
    }
    int64_t now = now_us(d);
    bool ok = echoy_band_link_update(d->band, u, now);
    if (!ok) ++d->rejected_actions;
}
esp_err_t echoy_device_probe_get(echoy_device_handle_t d, echoy_device_diagnostics_t *out)
{
    if (!d || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(d->view_lock, portMAX_DELAY);
    *out = d->probe_cache;
    out->requested_generation = atomic_load(&d->probe_sequence);
    out->condition = d->cache.condition;
    out->volume_percent = (unsigned)lround(d->cache.volume * 100);
    xSemaphoreGive(d->view_lock);
    return ESP_OK;
}
esp_err_t echoy_device_probe_request(echoy_device_handle_t d, bool start)
{
#if !CONFIG_ECHOY_AUDIO_DIAGNOSTICS
    (void)d;
    (void)start;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!d) {
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_load(&d->stopping) || !atomic_load(&d->activated)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (start) {
        xSemaphoreTake(d->view_lock, portMAX_DELAY);
        bool ready = d->cache.condition == ECHOY_NORMAL;
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
        ready &= !atomic_load(&d->measure_busy);
#endif
        bool admitted = ready && !atomic_exchange(&d->probe_busy, true);
        xSemaphoreGive(d->view_lock);
        if (!admitted) {
            return ESP_ERR_INVALID_STATE;
        }
    } else if (!atomic_load(&d->probe_busy)) {
        return ESP_OK;
    }
    unsigned generation =
        start ? atomic_fetch_add(&d->probe_sequence, 1) + 1 : atomic_load(&d->probe_sequence);
    command_t command = {
        .probe_command = start ? 1 : 2, .probe_generation = generation, .at = now_us(d)};
    if (xQueueSend(d->commands, &command, 0) != pdTRUE) {
        if (start) {
            atomic_store(&d->probe_busy, false);
        }
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#endif
}
esp_err_t echoy_device_measure_get(echoy_device_handle_t d,
                                   echoy_output_measurement_status_t *out)
{
    if (!d || !out) return ESP_ERR_INVALID_ARG;
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    xSemaphoreTake(d->view_lock, portMAX_DELAY);
    *out = d->measure_cache;
    out->requested_generation = atomic_load(&d->measure_sequence);
    out->runs_started = atomic_load(&measure_boot_runs);
    out->runs_remaining = ECHOY_OUTPUT_MEASUREMENT_MAX_RUNS - out->runs_started;
    if (out->result_ready && atomic_load(&d->measure_cancel_sequence) >= out->generation) {
        out->phase = ECHOY_OUTPUT_MEASUREMENT_CANCELLED;
        out->result_ready = false;
    }
    xSemaphoreGive(d->view_lock);
#else
    *out = (echoy_output_measurement_status_t){.phase=ECHOY_OUTPUT_MEASUREMENT_DISABLED};
#endif
    return ESP_OK;
}
esp_err_t echoy_device_measure_result(echoy_device_handle_t d, unsigned run_id,
                                      echoy_output_measurement_result_t *out)
{
    if (!d || !out || !run_id) return ESP_ERR_INVALID_ARG;
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    xSemaphoreTake(d->view_lock, portMAX_DELAY);
    bool ready = d->measure_cache.result_ready && d->measure_cache.generation == run_id &&
                 d->measure_cache.phase == ECHOY_OUTPUT_MEASUREMENT_COMPLETE &&
                 atomic_load(&d->measure_cancel_sequence) < run_id;
    if (ready) *out = *d->measure_result_cache;
    xSemaphoreGive(d->view_lock);
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
esp_err_t echoy_device_measure_request(echoy_device_handle_t d, bool start, unsigned gain,
                                       echoy_output_measurement_status_t *out)
{
#if !CONFIG_ECHOY_OUTPUT_MEASUREMENT
    (void)d; (void)start; (void)gain; (void)out;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!d || (start && (gain > 30 || gain % 6))) return ESP_ERR_INVALID_ARG;
    if (atomic_load(&d->stopping) || !atomic_load(&d->activated)) return ESP_ERR_INVALID_STATE;
    if (!start) {
        unsigned generation = atomic_load(&d->measure_sequence);
        if (!atomic_load(&d->measure_busy)) {
            if (out) echoy_device_measure_get(d, out);
            return ESP_OK;
        }
        /* Invalidate before queue admission: even a dropped Cancel stops I/O. */
        measure_cancel_generation(d, generation);
        command_t command = {.measure_command=2, .measure_generation=generation};
        if (xQueueSend(d->commands, &command, 0) != pdTRUE) {
            atomic_store(&d->queue_fault, true);
            return ESP_ERR_NO_MEM;
        }
        if (out) echoy_device_measure_get(d, out);
        return ESP_OK;
    }
    xSemaphoreTake(d->view_lock, portMAX_DELAY);
    if (atomic_load(&d->measure_busy) || atomic_load(&d->probe_busy) ||
        atomic_load(&d->stopping) || d->cache.condition != ECHOY_NORMAL ||
        atomic_load(&measure_boot_runs) >= ECHOY_OUTPUT_MEASUREMENT_MAX_RUNS) {
        xSemaphoreGive(d->view_lock);
        return ESP_ERR_INVALID_STATE;
    }
    unsigned previous_runs = atomic_load(&measure_boot_runs);
    do {
        if (previous_runs >= ECHOY_OUTPUT_MEASUREMENT_MAX_RUNS) {
            xSemaphoreGive(d->view_lock);
            return ESP_ERR_INVALID_STATE;
        }
    } while (!atomic_compare_exchange_weak(&measure_boot_runs, &previous_runs, previous_runs + 1));
    unsigned runs = previous_runs + 1;
    unsigned generation = atomic_fetch_add(&measure_boot_sequence, 1) + 1;
    atomic_store(&d->measure_sequence, generation);
    atomic_store(&d->measure_busy, true);
    command_t command = {.measure_command=1, .measure_generation=generation, .measure_gain=gain};
    if (xQueueSend(d->commands, &command, 0) != pdTRUE) {
        atomic_fetch_sub(&measure_boot_runs, 1);
        atomic_store(&d->measure_busy, false);
        atomic_store(&d->queue_fault, true);
        xSemaphoreGive(d->view_lock);
        return ESP_ERR_NO_MEM;
    }
    d->measure_cache = (echoy_output_measurement_status_t){
        .phase=ECHOY_OUTPUT_MEASUREMENT_STARTING, .generation=generation,
        .requested_generation=generation, .runs_started=runs,
        .runs_remaining=ECHOY_OUTPUT_MEASUREMENT_MAX_RUNS-runs, .input_gain_db=(float)gain};
    if (out) *out = d->measure_cache;
    xSemaphoreGive(d->view_lock);
    return ESP_OK;
#endif
}
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
static bool measure_cancelled(void *ctx)
{
    struct echoy_device *d = ctx;
    return atomic_load(&d->stopping) ||
           atomic_load(&d->measure_cancel_sequence) >= d->measure_job_generation;
}
static void measure_mark_end(struct echoy_device *d,
    echoy_output_measurement_phase_t phase, esp_err_t error)
{
    d->measure_pending = false;
    d->measure_ending = true;
    d->measure_terminal = phase;
    d->measure.error_code = error;
    d->measure.result_ready = false;
    if (phase != ECHOY_OUTPUT_MEASUREMENT_COMPLETE)
        measure_cancel_generation(d, atomic_load(&d->measure_sequence));
}
static void measure_clear_pcm(struct echoy_device *d)
{
    /* Volatile stores also erase the buffer immediately before it is freed. */
    volatile int16_t *samples = d->measure_pcm;
    for (size_t i = 0; i < ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES; ++i) samples[i] = 0;
}
#endif
static void finish_probe(struct echoy_device *d, echoy_probe_phase_t phase, esp_err_t error)
{
    unsigned generation = atomic_load(&d->probe_sequence);
    if (d->probe.generation != generation) {
        d->probe = (echoy_device_diagnostics_t){.generation = generation};
    }
    d->probe_pending = d->probe_active = false;
    d->probe.phase = phase;
    d->probe.error = error;
    atomic_store(&d->probe_busy, false);
}
static void publish(struct echoy_device *d)
{
    int64_t now=now_us(d);
    bool link_due=now>=d->next_link_publish_us;
    if (d->band) {
        echoy_band_read(d->band, ECHOY_TILE_A, &d->staging);
        if (link_due) {
            int64_t before = now_us(d);
            echoy_band_link_read(d->band,&d->link_view_staging);
            if (d->trace_active) {
                d->trace_active->flags |= TRACE_LINK_VIEW;
                d->trace_active->link_view_us = (uint32_t)trace_elapsed(now_us(d), before);
            }
        }
    }
    board_health_snapshot_t health;
    bool known = d->health && board_health_get(d->health, &health) == ESP_OK && health.valid;
    d->staging.battery_unknown = !known;
    d->staging.battery = known ? (unsigned)health.percentage : 0;
    d->staging.imu_unknown = true;
    d->staging.density_rejected_request = d->density_rejected_request;
    d->staging.condition = d->condition;
    d->staging.state.fault = d->condition != ECHOY_NORMAL;
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    d->staging.measurement_active = atomic_load(&d->measure_busy);
    d->staging.measurement_microphone = d->measure_io;
#endif
    int64_t wait_started = now_us(d);
    /* A preempted UI/radio reader must not delay the PCM deadline. Keep its
     * coherent previous snapshot and retry publication on the next block. */
    if (xSemaphoreTake(d->view_lock, 0) != pdTRUE) {
        ++d->cache_busy;
        if (d->trace_active) {
            d->trace_active->flags |= TRACE_CACHE_BUSY;
            d->trace_active->mutex_wait_us = (uint32_t)trace_elapsed(now_us(d), wait_started);
        }
        return;
    }
    int64_t acquired = now_us(d);
    d->cache = d->staging;
    d->probe_cache = d->probe;
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    if (d->measure.generation == atomic_load(&d->measure_sequence)) {
        if (d->measure.result_ready &&
            atomic_load(&d->measure_cancel_sequence) >= d->measure.generation) {
            d->measure.phase = ECHOY_OUTPUT_MEASUREMENT_CANCELLED;
            d->measure.result_ready = d->measure_publish_result = false;
        }
        if (d->measure_publish_result) {
            echoy_output_measurement_result_t *previous = d->measure_result_cache;
            d->measure_result_cache = d->measure_work;
            d->measure_work = previous;
            d->measure_publish_result = false;
        }
        d->measure_cache = d->measure;
        d->measure_cache.runs_started = atomic_load(&measure_boot_runs);
        d->measure_cache.runs_remaining =
            ECHOY_OUTPUT_MEASUREMENT_MAX_RUNS - d->measure_cache.runs_started;
        if (!d->measure_ending && !d->measure_job_in_flight &&
            d->measure.phase >= ECHOY_OUTPUT_MEASUREMENT_COMPLETE) {
            atomic_store(&d->measure_busy, false);
            d->cache.measurement_active = false;
        }
    }
#endif
    if (link_due) {
        d->link_view_staging.available=d->band && d->condition==ECHOY_NORMAL;
        d->link_cache=d->link_view_staging;
        d->next_link_publish_us=now+50000;
    }
    int64_t copied = now_us(d);
    xSemaphoreGive(d->view_lock);
    if (d->trace_active) {
        d->trace_active->mutex_wait_us = (uint32_t)trace_elapsed(acquired, wait_started);
        d->trace_active->cache_copy_us = (uint32_t)trace_elapsed(copied, acquired);
    }
}
static void set_condition(struct echoy_device *d, echoy_condition_t condition)
{
    if (d->condition != condition) {
        d->condition = condition;
        if (d->band) {
            echoy_band_action(d->band, ECHOY_TILE_A, ECHOY_ACT_CONDITION, condition, now_us(d));
        }
    }
}
static void fail_audio(struct echoy_device *d, board_audio_t **audio, const char *operation,
                       esp_err_t error)
{
    d->audio_fault = true;
    echoy_device_pcm_reset(&d->pcm_processor);
    echoy_pickup_pcm_reset(&d->pickup_processor);
    if (atomic_load(&d->probe_busy)) {
        finish_probe(d, ECHOY_PROBE_FAILED, error);
    }
    set_condition(d, ECHOY_AUDIO_FAULT);
    if (*audio) {
        board_audio_get_stats(*audio, &d->last_audio_stats);
    }
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    if (d->measure_io)
        d->measure.elapsed_us = (uint64_t)(now_us(d) - d->measure_started_us);
#endif
    const esp_err_t close_error = board_audio_close(audio);
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    if (atomic_load(&d->measure_busy)) {
        if (d->measure.generation != atomic_load(&d->measure_sequence))
            d->measure = (echoy_output_measurement_status_t){
                .generation=atomic_load(&d->measure_sequence)};
        d->measure_io = false;
        measure_mark_end(d, ECHOY_OUTPUT_MEASUREMENT_FAILED, error);
    }
#endif
    if (d->trace_pending) {
        trace_log_deadline(d, error);
    } else {
        ESP_LOGE(TAG, "%s failed: %s; playback stopped; explicit recovery required", operation,
                 esp_err_to_name(error));
        ESP_LOGE(
            TAG,
            "accepted=%llu nonzero=%llu peak=%u pa=%u activations=%u partial=%u "
            "write_errors=%u last_write_us=%u max_write_us=%u",
            (unsigned long long)d->last_audio_stats.frames_written,
            (unsigned long long)d->last_audio_stats.nonzero_frames,
            (unsigned)d->last_audio_stats.peak_pcm, (unsigned)d->last_audio_stats.pa_enabled,
            (unsigned)d->last_audio_stats.pa_activations, (unsigned)d->last_audio_stats.partial_writes,
            (unsigned)d->last_audio_stats.write_failures, (unsigned)d->last_audio_stats.last_write_us,
            (unsigned)d->last_audio_stats.max_write_us);
        ESP_LOGE(TAG, "audio timing: render_max=%lldus mix_max=%lldus convert_max=%lldus core=%d",
                 (long long)d->max_render_us, (long long)d->max_mix_us,
                 (long long)d->max_convert_us, d->audio_core);
        echoy_band_profile_t profile = echoy_band_render_profile(d->band);
        ESP_LOGE(TAG, "first mix cycles: clock=%u voice=%u cues=%u commits=%u",
                 (unsigned)profile.clock_cycles, (unsigned)profile.voice_cycles,
                 (unsigned)profile.cue_cycles, (unsigned)profile.tick_calls);
    }
    if (close_error != ESP_OK) {
        /* Cleanup failure can leave hardware ownership uncertain. Do not reopen. */
        d->fatal = true;
        d->shutdown_error = close_error;
        ESP_LOGE(TAG, "audio cleanup failed: %s; Recovery remains available",
                 esp_err_to_name(close_error));
    }
}
static bool open_audio(struct echoy_device *d, board_audio_t **audio)
{
    esp_err_t error = board_audio_open(audio);
    if (error != ESP_OK) {
        fail_audio(d, audio, "speaker initialization", error);
        return false;
    }
    d->audio_fault = false;
    echoy_device_pcm_reset(&d->pcm_processor);
    echoy_pickup_pcm_reset(&d->pickup_processor);
    d->last_audio_stats = (board_audio_stats_t){0};
    d->accepted_frames = 0;
    /* I2S owns three 5 ms DMA descriptors. This is an estimated presentation
     * timeline, not a measured DAC playhead. Hardware acceptance measures it. */
    d->origin_us = now_us(d) + QUEUE_US;
    if (d->trace_frozen) {
        /* Explicit successful recovery begins a fresh diagnostic history. */
        d->trace_frozen = d->trace_pending = false;
        d->trace_next = d->trace_count = 0;
        trace_begin(d);
    }
    return true;
}
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
static void measure_close(struct echoy_device *d, board_audio_t **audio)
{
    if (!d->measure_io) return;
    esp_err_t error = board_audio_close(audio);
    d->measure_io = false;
    d->measure.elapsed_us = (uint64_t)(now_us(d) - d->measure_started_us);
    if (error != ESP_OK) {
        d->fatal = true;
        d->shutdown_error = error;
        fail_audio(d, audio, "measurement shutdown", error);
    } else if (!atomic_load(&d->stopping) && !d->audio_fault) {
        open_audio(d, audio); /* Only silent TX is restored, never playback permission. */
    }
}
static void measure_progress(struct echoy_device *d, board_audio_t **audio)
{
    if (!atomic_load(&d->measure_busy)) return;
    unsigned generation = atomic_load(&d->measure_sequence);
    bool cancelled = atomic_load(&d->measure_cancel_sequence) >= generation;
    if (d->measure.generation != generation) return; /* FIFO admission still pending. */
    if (cancelled && d->measure.phase == ECHOY_OUTPUT_MEASUREMENT_COMPLETE) {
        d->measure.phase = ECHOY_OUTPUT_MEASUREMENT_CANCELLED;
        d->measure.result_ready = d->measure_publish_result = false;
    }
    if ((d->measure_pending || d->measure_io) &&
        (cancelled || d->condition != ECHOY_NORMAL)) {
        measure_mark_end(d, d->condition == ECHOY_NORMAL ? ECHOY_OUTPUT_MEASUREMENT_CANCELLED
                                                       : ECHOY_OUTPUT_MEASUREMENT_FAILED,
                         d->condition == ECHOY_NORMAL ? ESP_OK : ESP_ERR_INVALID_STATE);
    }
    if (d->measure_pending && !d->measure_ending) {
        d->measure_pending = false;
        /* Copy the paused model once before RX is opened. During capture only
         * the small status/state fields are published, never a whole view. */
        echoy_band_read(d->band, ECHOY_TILE_A, &d->staging);
        esp_err_t error = board_audio_close(audio);
        if (error != ESP_OK) {
            d->fatal = true; d->shutdown_error = error;
            fail_audio(d, audio, "measurement speaker handoff", error);
        } else if (atomic_load(&d->stopping) ||
                   atomic_load(&d->measure_cancel_sequence) >= generation) {
            measure_mark_end(d, ECHOY_OUTPUT_MEASUREMENT_CANCELLED, ESP_OK);
            if (!atomic_load(&d->stopping)) open_audio(d, audio);
        } else {
            d->measure_started_us = now_us(d);
            d->measure_deadline_us = d->measure_started_us +
                (int64_t)ECHOY_OUTPUT_MEASUREMENT_HARD_LIMIT_MS * 1000;
            error = board_audio_measure_open(audio, (unsigned)d->measure.input_gain_db,
                                              d->measure_deadline_us);
            if (error != ESP_OK) {
                fail_audio(d, audio, "measurement initialization", error);
            } else {
                d->measure_io = true;
                d->measure.phase = ECHOY_OUTPUT_MEASUREMENT_CAPTURING;
            }
        }
    }
    if (d->measure_ending) {
        measure_close(d, audio);
        if (!d->measure_job_in_flight) {
            d->measure_analyze = d->measure_terminal == ECHOY_OUTPUT_MEASUREMENT_COMPLETE;
            d->measure_job_generation = generation;
            d->measure_job_in_flight = true;
            d->measure.phase = d->measure_analyze ? ECHOY_OUTPUT_MEASUREMENT_ANALYZING
                                                : ECHOY_OUTPUT_MEASUREMENT_STOPPING;
            atomic_store(&d->measure_job_pending, true);
        }
        if (atomic_exchange(&d->measure_job_done, false)) {
            d->measure_job_in_flight = false;
            d->measure_ending = false;
            if (d->measure_terminal == ECHOY_OUTPUT_MEASUREMENT_COMPLETE) {
                if (cancelled) d->measure_terminal = ECHOY_OUTPUT_MEASUREMENT_CANCELLED;
                else if (!d->measure_analysis_ok) {
                    d->measure_terminal = ECHOY_OUTPUT_MEASUREMENT_FAILED;
                    d->measure.error_code = ESP_ERR_INVALID_RESPONSE;
                }
            }
            d->measure.phase = d->measure_terminal;
            d->measure.result_ready = d->measure_terminal == ECHOY_OUTPUT_MEASUREMENT_COMPLETE;
            d->measure_publish_result = d->measure.result_ready;
            if (d->band) echoy_band_action(d->band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, now_us(d));
        }
    }
}
static void measure_publish_capture(struct echoy_device *d)
{
    if (xSemaphoreTake(d->view_lock, 0) != pdTRUE) {
        ++d->cache_busy;
        return;
    }
    d->measure_cache = d->measure;
    d->cache.measurement_active = true;
    d->cache.measurement_microphone = true;
    d->cache.state = d->staging.state;
    xSemaphoreGive(d->view_lock);
}
static void measure_block(struct echoy_device *d, board_audio_t **audio)
{
    unsigned generation = atomic_load(&d->measure_sequence);
    if (atomic_load(&d->stopping) || atomic_load(&d->measure_cancel_sequence) >= generation) {
        measure_mark_end(d, ECHOY_OUTPUT_MEASUREMENT_CANCELLED, ESP_OK);
        measure_progress(d, audio);
        return;
    }
    if (now_us(d) >= d->measure_deadline_us) {
        fail_audio(d, audio, "measurement hard deadline", ESP_ERR_TIMEOUT);
        return;
    }
    size_t offset = d->measure.frames_captured;
    echoy_output_measurement_render(offset, d->block->pcm, BLOCK_FRAMES);
    size_t written = 0;
    esp_err_t error = board_audio_write(*audio, d->block->pcm, BLOCK_FRAMES, &written);
    d->measure.frames_written += written;
    if (error != ESP_OK || written != BLOCK_FRAMES) {
        fail_audio(d, audio, "measurement PCM output", error != ESP_OK ? error : ESP_ERR_INVALID_SIZE);
        return;
    }
    if (now_us(d) >= d->measure_deadline_us) {
        fail_audio(d, audio, "measurement hard deadline", ESP_ERR_TIMEOUT);
        return;
    }
    if (atomic_load(&d->stopping) || atomic_load(&d->measure_cancel_sequence) >= generation) {
        measure_mark_end(d, ECHOY_OUTPUT_MEASUREMENT_CANCELLED, ESP_OK);
        measure_progress(d, audio);
        return;
    }
    size_t read = 0;
    error = board_audio_measure_read(*audio, d->measure_pcm + offset, BLOCK_FRAMES, &read);
    d->measure.frames_captured += read;
    d->measure.elapsed_us = (uint64_t)(now_us(d) - d->measure_started_us);
    if (error != ESP_OK || read != BLOCK_FRAMES) {
        fail_audio(d, audio, "measurement PCM input", error != ESP_OK ? error : ESP_ERR_INVALID_SIZE);
    } else if (now_us(d) >= d->measure_deadline_us) {
        fail_audio(d, audio, "measurement hard deadline", ESP_ERR_TIMEOUT);
    } else if (d->measure.frames_captured == ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES) {
        measure_mark_end(d, ECHOY_OUTPUT_MEASUREMENT_COMPLETE, ESP_OK);
        measure_progress(d, audio);
    }
}
static void measure_prepare(struct echoy_device *d)
{
    if (!atomic_exchange(&d->measure_job_pending, false)) return;
    d->measure_analysis_ok = d->measure_analyze && echoy_output_measurement_analyze(
        d->measure_pcm, ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES, d->measure_work,
        measure_cancelled, d);
    /* No raw microphone recording survives analysis or a cancelled run. */
    measure_clear_pcm(d);
    if (!d->measure_analysis_ok || measure_cancelled(d))
        memset(d->measure_work, 0, sizeof(*d->measure_work));
    atomic_store(&d->measure_job_done, true);
}
#endif
static void prepare_worker(void *arg)
{
    struct echoy_device *d = arg;
#if CONFIG_ECHOY_OFFICE_VOICING_TRIAL
    const float *role_gains = echoy_office_role_gains;
    const bool preview_speaker=true;
#else
    const float *role_gains = echoy_device_role_gains;
    const bool preview_speaker=false;
#endif
    xEventGroupWaitBits(d->done, STARTED, pdFALSE, pdTRUE, portMAX_DELAY);
    echoy_band_t *band = atomic_load(&d->stopping) ? NULL :
                        echoy_band_create_configured(preview_speaker,role_gains);
    if (band) echoy_band_enable_pickup(band, CONFIG_ECHOY_PHRASE_PICKUP);
    xQueueSend(d->startup, &band, portMAX_DELAY);
    ESP_LOGI(TAG, "musical role gain milli: pulse=%u voice=%u dust=%u harmony=%u",
             (unsigned)(role_gains[0]*1000), (unsigned)(role_gains[1]*1000),
             (unsigned)(role_gains[2]*1000), (unsigned)(role_gains[3]*1000));
    while (!atomic_load(&d->stopping)) {
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
        measure_prepare(d);
#endif
        char diagnostic[DIAGNOSTIC_BYTES];
        if (xQueueReceive(d->diagnostics, diagnostic, 0) == pdTRUE) {
            ESP_LOGI(TAG, "%s", diagnostic);
            ESP_LOGI(TAG, "%s prep_stack=%u", diagnostic + DIAGNOSTIC_BYTES / 2,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
        echoy_band_job_t *job = NULL;
        if (xQueueReceive(d->jobs, &job, ticks(10)) != pdTRUE) {
            continue;
        }
        int64_t start = now_us(d);
        prepared_t result = {.job = job, .ok = echoy_band_prepare_job(job)};
#if CONFIG_ECHOY_OFFICE_VOICING_TRIAL
        if (result.ok) result.ok = echoy_band_voice_for_speaker_job(job);
#elif CONFIG_ECHOY_HARMONY_BASS_TRIAL
        if (result.ok) result.ok = echoy_band_cut_harmony_bass_job(job);
#endif
        if (result.ok) {
            result.ok = echoy_band_calibrate_job(job, role_gains);
            result.calibration_failed = !result.ok;
        }
        if (result.ok) result.ok=echoy_band_prepare_pickup_job(job);
        result.elapsed_us = now_us(d) - start;
        if (!result.ok) {
            ESP_LOGE(TAG, "sound preparation/calibration failed");
        }
        /* Only one job is in flight. The completion slot cannot be full. */
        if (xQueueSend(d->prepared, &result, 0) != pdTRUE) {
            echoy_band_discard_job(job);
            atomic_store(&d->queue_fault, true);
        }
        vTaskDelay(1);
    }
    xEventGroupSetBits(d->done, PREPARE_DONE);
    atomic_store(&d->prepare_exited, true);
    vTaskDelete(NULL);
}
static void update_health(struct echoy_device *d)
{
    board_health_snapshot_t health;
    if (!d->health || board_health_get(d->health, &health) != ESP_OK) {
        return;
    }
    if (board_health_battery_empty(&health)) {
        d->battery_empty = true;
    } else if ((health.valid && health.percentage > 0) ||
               (health.present_known && !health.present)) {
        d->battery_empty = false;
    }
    /* A read error cannot turn an actual empty battery into permission to play. */
}
static bool process_commands(struct echoy_device *d, board_audio_t **audio)
{
    bool flush = false;
    command_t command;
    if (atomic_exchange(&d->queue_fault, false)) {
        ++d->queue_overflows;
        while (xQueueReceive(d->commands, &command, 0) == pdTRUE) {
        }
        fail_audio(d, audio, "control queue overflow", ESP_ERR_NO_MEM);
        return false;
    }
    /* Bounded batch preserves FIFO order (especially Record then Cancel). */
    for (unsigned i = 0; i < COMMAND_COUNT && xQueueReceive(d->commands, &command, 0) == pdTRUE;
         ++i) {
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
        if (command.measure_command) {
            if (command.measure_generation != atomic_load(&d->measure_sequence) ||
                !atomic_load(&d->measure_busy)) continue;
            if (command.measure_command == 1) {
                d->measure = (echoy_output_measurement_status_t){
                    .phase=ECHOY_OUTPUT_MEASUREMENT_STARTING,
                    .generation=command.measure_generation, .input_gain_db=(float)command.measure_gain};
                d->measure_publish_result = false;
                if (d->band) echoy_band_action(d->band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, now_us(d));
                if (d->band && *audio && d->condition == ECHOY_NORMAL &&
                    !atomic_load(&d->probe_busy)) {
                    d->measure_pending = true;
                    flush = true;
                } else {
                    measure_mark_end(d, ECHOY_OUTPUT_MEASUREMENT_FAILED, ESP_ERR_INVALID_STATE);
                }
            }
            continue;
        }
#endif
        if (command.probe_command) {
            if (command.probe_generation != atomic_load(&d->probe_sequence) ||
                !atomic_load(&d->probe_busy)) {
                continue;
            }
            if (command.probe_command == 2) {
                if (atomic_load(&d->probe_busy)) {
                    finish_probe(d, ECHOY_PROBE_CANCELLED, ESP_OK);
                    flush = true;
                }
            } else if (d->band && *audio && d->condition == ECHOY_NORMAL) {
                echoy_band_action(d->band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, now_us(d));
                d->probe = (echoy_device_diagnostics_t){.phase = ECHOY_PROBE_QUEUED,
                                                        .generation = command.probe_generation};
                d->probe_pending = true;
                flush = true;
            } else {
                finish_probe(d, ECHOY_PROBE_FAILED, ESP_ERR_INVALID_STATE);
            }
            continue;
        }
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
        if (command.cancels_measurement) {
            if (command.measure_generation == atomic_load(&d->measure_sequence)) {
                measure_cancel_generation(d, command.measure_generation);
                if (d->measure.phase == ECHOY_OUTPUT_MEASUREMENT_COMPLETE) {
                    d->measure.phase = ECHOY_OUTPUT_MEASUREMENT_CANCELLED;
                    d->measure.result_ready = d->measure_publish_result = false;
                }
            }
            if (command.action == ECHOY_ACT_DENSITY)
                d->density_rejected_request = (uint64_t)command.at + 1;
            /* The cancelling touch is consumed; a separate later touch may play. */
            continue;
        }
#endif
        /* A real UI action interrupts the diagnostic, preserving user control. */
        if (atomic_load(&d->probe_busy)) {
            finish_probe(d, ECHOY_PROBE_CANCELLED, ESP_OK);
            flush = true;
        }
        if (command.action == ECHOY_ACT_CONDITION) {
            if (command.value == ECHOY_AUDIO_FAULT) {
                fail_audio(d, audio, "UI/controller", ESP_FAIL);
            } else if (command.value == ECHOY_NORMAL && d->audio_fault && !d->fatal &&
                       !d->battery_empty) {
                open_audio(d, audio);
            }
            /* Loading/battery states come from actual adapters, not commands. */
            continue;
        }
        if (!d->band || d->condition != ECHOY_NORMAL) {
            if (command.action == ECHOY_ACT_DENSITY)
                d->density_rejected_request = (uint64_t)command.at + 1;
            ++d->rejected_actions;
            continue;
        }
        /* Preserve input timestamps; the shared model already provides the
         * approved 60 ms lead before the first count-in cue. */
        bool accepted =
            echoy_band_action(d->band, ECHOY_TILE_A, command.action, command.value, command.at);
        if (!accepted) {
            if (command.action == ECHOY_ACT_DENSITY)
                d->density_rejected_request = (uint64_t)command.at + 1;
            ++d->rejected_actions;
            continue;
        }
        if (command.action == ECHOY_ACT_PAUSE || command.action == ECHOY_ACT_CANCEL_RECORD ||
            command.action == ECHOY_ACT_RECORD || command.action == ECHOY_ACT_EXAMPLE ||
            command.action == ECHOY_ACT_AUDITION) {
            flush = true;
        } else if (command.action == ECHOY_ACT_TRANSPORT) {
            echoy_band_read(d->band, ECHOY_TILE_A, &d->staging);
            flush |= d->staging.state.phase == ECHOY_IDLE;
        }
    }
    return flush;
}
static void log_resources(struct echoy_device *d, board_audio_t *audio)
{
    int64_t now = now_us(d);
    if (now < d->next_log_us) {
        return;
    }
    d->next_log_us = now + INT64_C(30000000);
    board_audio_stats_t stats = d->last_audio_stats;
    if (audio) {
        board_audio_get_stats(audio, &stats);
    }
    char diagnostic[DIAGNOSTIC_BYTES] = {0};
    snprintf(diagnostic, DIAGNOSTIC_BYTES / 2,
             "pcm_gain_milli=%u clipped=%llu accepted=%llu nonzero=%llu peak=%u pa=%u "
             "activations=%u write_max=%uus partial=%u write_errors=%u deadline_misses=%u",
             (unsigned)(ECHOY_DEVICE_PCM_GAIN * 1000), (unsigned long long)d->pcm_clipped,
             (unsigned long long)stats.frames_written, (unsigned long long)stats.nonzero_frames,
             (unsigned)stats.peak_pcm, (unsigned)stats.pa_enabled, (unsigned)stats.pa_activations,
             (unsigned)stats.max_write_us, (unsigned)stats.partial_writes,
             (unsigned)stats.write_failures, d->deadline_misses);
    snprintf(diagnostic + DIAGNOSTIC_BYTES / 2, DIAGNOSTIC_BYTES / 2,
             "internal=%u PSRAM=%u owner_stack=%u prepare=%lldus render_max=%lldus rejected=%u "
             "overflow=%u cache_busy=%u limited=%llu mix=%lldus pcm=%lldus core=%d",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (long long)d->last_prepare_us,
             (long long)d->max_render_us, d->rejected_actions, d->queue_overflows, d->cache_busy,
             (unsigned long long)d->pcm_limited, (long long)d->max_mix_us,
             (long long)d->max_convert_us, d->audio_core);
    /* UART logging may block for longer than a PCM block. Worker owns all
     * periodic log output; dropping a diagnostic is safer than blocking audio. */
    xQueueOverwrite(d->diagnostics, diagnostic);
}
static void owner_worker(void *arg)
{
    struct echoy_device *d = arg;
    board_audio_t *audio = NULL;
    xEventGroupWaitBits(d->done, STARTED, pdFALSE, pdTRUE, portMAX_DELAY);
    while (xQueueReceive(d->startup, &d->band, ticks(10)) != pdTRUE) {
        /* Always accept ownership, including a stop during startup. */
    }
    if (!d->band && !atomic_load(&d->stopping)) {
        d->fatal = d->audio_fault = true;
        ESP_LOGE(TAG, "shared controller allocation failed; Recovery remains available");
    } else if (!atomic_load(&d->stopping)) {
        open_audio(d, &audio);
    }
    while (!atomic_load(&d->stopping)) {
        trace_begin(d);
        update_health(d);
        trace_mark(d, TRACE_HEALTH);
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
        if (!d->measure_io)
#endif
            process_link(d);
        trace_mark(d, TRACE_LINK);
        bool flush = process_commands(d, &audio);
        trace_mark(d, TRACE_COMMANDS);
        echoy_condition_t desired = d->audio_fault     ? ECHOY_AUDIO_FAULT
                                    : d->battery_empty ? ECHOY_BATTERY_EMPTY
                                                       : ECHOY_NORMAL;
        flush |= desired != d->condition;
        set_condition(d, desired);
        if (desired != ECHOY_NORMAL && atomic_load(&d->probe_busy)) {
            finish_probe(d, ECHOY_PROBE_FAILED, ESP_ERR_INVALID_STATE);
        }
        if (flush) {
            if (d->trace_active) d->trace_active->flags |= TRACE_FLUSH;
            echoy_device_pcm_reset(&d->pcm_processor);
            echoy_pickup_pcm_reset(&d->pickup_processor);
        }
        if (flush && audio
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
            && !d->measure_io
#endif
        ) {
            esp_err_t error = board_audio_flush(audio);
            if (error != ESP_OK) {
                fail_audio(d, &audio, "clear queued sound", error);
            } else {
                d->accepted_frames = 0;
                d->origin_us = now_us(d) + QUEUE_US;
            }
        }
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
        measure_progress(d, &audio);
        if (d->measure_io) {
            measure_publish_capture(d);
            measure_block(d, &audio);
            /* RX has only its ordinary bounded DMA queue. Defer musical job
             * completion, link snapshots, UI view copies and logging until
             * capture is closed; each round services TX/RX immediately. */
            continue;
        }
#endif
#if CONFIG_ECHOY_AUDIO_DIAGNOSTICS
        if (d->probe_pending && audio && d->condition == ECHOY_NORMAL) {
            esp_err_t error = board_audio_readback(audio, &d->probe.before);
            if (error != ESP_OK) {
                fail_audio(d, &audio, "diagnostic codec readback", error);
            } else {
                d->probe_pending = false;
                d->probe_active = true;
                d->probe_position = 0;
                d->probe.phase = ECHOY_PROBE_PLAYING;
                d->accepted_frames = 0;
                d->origin_us = now_us(d) + QUEUE_US;
            }
        }
        if (d->probe_active && d->probe_position >= ECHOY_PROBE_FRAMES + ECHOY_PROBE_TAIL_FRAMES) {
            esp_err_t error = board_audio_readback(audio, &d->probe.after);
            if (error == ESP_OK) {
                echoy_device_pcm_reset(&d->pcm_processor);
                echoy_pickup_pcm_reset(&d->pickup_processor);
                error = board_audio_flush(audio);
            }
            if (error != ESP_OK) {
                fail_audio(d, &audio, "diagnostic completion", error);
            } else {
                finish_probe(d, ECHOY_PROBE_DONE, ESP_OK);
                d->accepted_frames = 0;
                d->origin_us = now_us(d) + QUEUE_US;
            }
        }
        if (audio) {
            board_audio_get_stats(audio, &d->probe.output);
        }
#endif
        prepared_t result;
        if (xQueueReceive(d->prepared, &result, 0) == pdTRUE) {
            if (d->trace_active) d->trace_active->flags |= TRACE_GOT_JOB;
            d->preparation_in_flight = false;
            d->last_prepare_us = result.elapsed_us;
            if (result.calibration_failed) {
                echoy_band_discard_job(result.job);
                fail_audio(d, &audio, "musical output calibration", ESP_ERR_INVALID_RESPONSE);
            } else {
                echoy_band_finish_job(d->band, result.job, now_us(d));
            }
        }
        trace_mark(d, TRACE_FINISH);
        if (d->band) {
            echoy_band_tick(d->band, now_us(d));
            trace_mark(d, TRACE_TICK);
            if (!d->preparation_in_flight) {
                echoy_band_job_t *job = echoy_band_take_job(d->band);
                if (job) {
                    if (d->trace_active) d->trace_active->flags |= TRACE_TOOK_JOB;
                    if (xQueueSend(d->jobs, &job, 0) == pdTRUE) {
                        d->preparation_in_flight = true;
                    } else {
                        echoy_band_discard_job(job);
                        fail_audio(d, &audio, "preparation queue", ESP_ERR_INVALID_STATE);
                    }
                }
            }
        }
        trace_mark(d, TRACE_TAKE);
        publish(d);
        trace_mark(d, TRACE_PUBLISH);
        log_resources(d, audio);
        if (!audio || d->condition != ECHOY_NORMAL || !d->band) {
            vTaskDelay(ticks(10));
            continue;
        }
        int64_t at =
            d->origin_us + (int64_t)(d->accepted_frames * 1000000 / BOARD_AUDIO_SAMPLE_RATE);
        int64_t start = now_us(d);
        if (d->trace_active) {
            d->trace_active->expected_us = at;
            d->trace_active->checked_us = start;
        }
#ifdef ESP_PLATFORM
        d->audio_core = xPortGetCoreID();
#else
        d->audio_core = -1;
#endif
        if (start > at) {
            /* Do not replay stale samples to hide a missed output deadline. */
            ++d->deadline_misses;
            trace_freeze(d, at, start);
            fail_audio(d, &audio, "estimated output deadline", ESP_ERR_TIMEOUT);
            continue;
        }
        if (
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
            atomic_load(&d->measure_busy)
#else
            false
#endif
        ) {
            memset(d->block->pcm, 0, sizeof(d->block->pcm));
        } else if (d->probe_active) {
            if (d->trace_active) d->trace_active->flags |= TRACE_PROBE;
            echoy_audio_probe_render(d->probe_position, d->block->pcm, BLOCK_FRAMES);
        } else {
            echoy_band_render_layers(d->band, at, d->block->floating,
                d->block->pickup, d->block->pickup_gate, BLOCK_FRAMES);
            int64_t mix_done = now_us(d);
            if (mix_done - start > d->max_mix_us) d->max_mix_us = mix_done - start;
            echoy_device_pcm_stats_t stats;
            if (!echoy_device_pcm_convert(&d->pcm_processor, d->block->floating, d->block->pcm,
                                          BLOCK_FRAMES, &stats)) {
                fail_audio(d, &audio, "nonfinite PCM", ESP_ERR_INVALID_RESPONSE);
            } else {
                d->pcm_clipped += stats.clipped;
                d->pcm_limited += stats.limited;
                if (!echoy_device_pcm_add_pickup(&d->pickup_processor,d->block->pickup,
                        d->block->pickup_gate,d->block->pcm,BLOCK_FRAMES))
                    fail_audio(d, &audio, "nonfinite pickup PCM", ESP_ERR_INVALID_RESPONSE);
            }
            int64_t converted = now_us(d) - mix_done;
            if (converted > d->max_convert_us) d->max_convert_us = converted;
        }
        if (!audio) {
            continue;
        }
        int64_t elapsed = now_us(d) - start;
        if (elapsed > d->max_render_us) {
            d->max_render_us = elapsed;
        }
        trace_mark(d, TRACE_RENDER);
        size_t written = 0;
        esp_err_t error = board_audio_write(audio, d->block->pcm, BLOCK_FRAMES, &written);
        trace_mark(d, TRACE_WRITE);
        d->accepted_frames += written;
        if (d->probe_active) {
            d->probe_position += written;
            d->probe.frames_accepted = d->probe_position < ECHOY_PROBE_FRAMES
                                           ? (unsigned)d->probe_position
                                           : ECHOY_PROBE_FRAMES;
        }
        if (error != ESP_OK || written != BLOCK_FRAMES) {
            fail_audio(d, &audio, "PCM output", error == ESP_OK ? ESP_ERR_INVALID_SIZE : error);
        }
    }
    atomic_store(&d->accepting, false);
    esp_err_t error = board_audio_close(&audio);
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    d->measure_io = false;
#endif
    if (error != ESP_OK) {
        d->shutdown_error = error;
        ESP_LOGE(TAG, "speaker shutdown: %s", esp_err_to_name(error));
    }
    xEventGroupSetBits(d->done, OWNER_DONE);
    atomic_store(&d->owner_exited, true);
    vTaskDelete(NULL);
}
static void release_context(struct echoy_device *d)
{
    echoy_band_job_t *job;
    prepared_t result;
    if (d->jobs && xQueueReceive(d->jobs, &job, 0) == pdTRUE) {
        echoy_band_discard_job(job);
    }
    if (d->prepared && xQueueReceive(d->prepared, &result, 0) == pdTRUE) {
        echoy_band_discard_job(result.job);
    }
    if (!d->band && d->startup) {
        xQueueReceive(d->startup, &d->band, 0);
    }
    echoy_band_delete(d->band);
    if (d->commands) {
        vQueueDelete(d->commands);
    }
    if (d->startup) {
        vQueueDelete(d->startup);
    }
    if (d->jobs) {
        vQueueDelete(d->jobs);
    }
    if (d->prepared) {
        vQueueDelete(d->prepared);
    }
    if (d->diagnostics) {
        vQueueDelete(d->diagnostics);
    }
    if (d->link_updates) vQueueDelete(d->link_updates);
    free(d->link_staging);
    if (d->view_lock) {
        vSemaphoreDelete(d->view_lock);
    }
    if (d->done) {
        vEventGroupDelete(d->done);
    }
    free(d->block);
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    if (d->measure_pcm) {
        measure_clear_pcm(d);
        free(d->measure_pcm);
    }
    free(d->measure_work);
    free(d->measure_result_cache);
#endif
    free(d);
}
esp_err_t echoy_device_start(echoy_device_handle_t *out, echoy_platform_t *platform)
{
    if (!out || !platform) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    struct echoy_device *d = heap_caps_calloc(1, sizeof(*d), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!d) {
        return ESP_ERR_NO_MEM;
    }
    atomic_init(&d->stopping, false);
    atomic_init(&d->queue_fault, false);
    atomic_init(&d->accepting, true);
    atomic_init(&d->owner_exited, false);
    atomic_init(&d->prepare_exited, false);
    atomic_init(&d->activated, false);
    atomic_init(&d->probe_busy, false);
    atomic_init(&d->probe_sequence, 0);
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    atomic_init(&d->measure_busy, false);
    atomic_init(&d->measure_job_pending, false);
    atomic_init(&d->measure_job_done, false);
    atomic_init(&d->measure_sequence, 0);
    atomic_init(&d->measure_cancel_sequence, 0);
    d->measure.phase = ECHOY_OUTPUT_MEASUREMENT_IDLE;
    d->measure_cache = d->measure;
    d->measure_pcm = heap_caps_calloc(ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES,
                                      sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    d->measure_work = heap_caps_calloc(1, sizeof(*d->measure_work), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    d->measure_result_cache = heap_caps_calloc(1, sizeof(*d->measure_result_cache),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!d->measure_pcm || !d->measure_work || !d->measure_result_cache) {
        release_context(d);
        return ESP_ERR_NO_MEM;
    }
#endif
    d->condition = ECHOY_LOADING;
    d->cache = (echoy_control_view_t){
        .memories = {1, 1, 1, 1},
        .present = true,
        .source = ECHOY_TILE_NONE,
        .volume = .65,
        .battery_unknown = true,
        .imu_unknown = true,
        .condition = ECHOY_LOADING,
        .plan = {.role = ECHOY_PULSE, .phrase = 1, .density = .5, .duration = 2.5},
        .state = {.role = ECHOY_PULSE, .phrase = 1, .density = .5, .fault = true}};
    d->staging = d->cache;
    d->commands = xQueueCreate(COMMAND_COUNT, sizeof(command_t));
    d->startup = xQueueCreate(1, sizeof(echoy_band_t *));
    d->jobs = xQueueCreate(1, sizeof(echoy_band_job_t *));
    d->prepared = xQueueCreate(1, sizeof(prepared_t));
    d->diagnostics = xQueueCreate(1, DIAGNOSTIC_BYTES);
    d->link_updates = xQueueCreate(1, sizeof(board_link_update_t));
    d->link_staging = heap_caps_calloc(1, sizeof(*d->link_staging), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    d->view_lock = xSemaphoreCreateMutex();
    d->done = xEventGroupCreate();
    d->block = heap_caps_calloc(1, sizeof(*d->block), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!d->commands || !d->startup || !d->jobs || !d->prepared || !d->diagnostics ||
        !d->view_lock || !d->done || !d->block || !d->link_updates || !d->link_staging) {
        release_context(d);
        return ESP_ERR_NO_MEM;
    }
    /* Tasks start suspended by a gate until both handles are available. */
    if (xTaskCreate(prepare_worker, "echoy_prepare", 12288, d, 1, &d->prepare_task) != pdPASS) {
        release_context(d);
        return ESP_ERR_NO_MEM;
    }
    /* Floating-point tasks become core-affine on this port. Wi-Fi runs at
     * priority 23 on core 0. On core 1, preempt GSP and the pinned TinyUSB /
     * Iris workers (priority 5): an equal-priority 10 ms time slice consumes
     * a complete PCM block even when its DMA completion is already queued. */
#ifdef ESP_PLATFORM
    BaseType_t owner_created = xTaskCreatePinnedToCore(
        owner_worker, "echoy_audio", 12288, d, AUDIO_OWNER_PRIORITY, &d->owner_task, 1);
#else
    BaseType_t owner_created = xTaskCreate(owner_worker, "echoy_audio", 12288, d,
                                          AUDIO_OWNER_PRIORITY, &d->owner_task);
#endif
    if (owner_created != pdPASS) {
        atomic_store(&d->stopping, true);
        xEventGroupSetBits(d->done, STARTED);
        xEventGroupWaitBits(d->done, PREPARE_DONE, pdFALSE, pdTRUE, portMAX_DELAY);
        while (!atomic_load(&d->prepare_exited)) {
            vTaskDelay(1);
        }
        release_context(d);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t health_error = board_health_start(NULL, &d->health);
    if (health_error != ESP_OK) {
        ESP_LOGW(TAG, "battery worker unavailable: %s; displaying unknown",
                 esp_err_to_name(health_error));
    }
    *platform = (echoy_platform_t){.ctx = d,
                                   .now_us = now_us,
                                   .ui_storage_alloc = ui_storage_alloc,
                                   .read_control = read_control,
                                   .perform_action = perform_action,
                                   .initial_condition = ECHOY_LOADING};
    *out = d;
    return ESP_OK;
}
esp_err_t echoy_device_activate(echoy_device_handle_t d)
{
    if (!d) {
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_load(&d->stopping) || atomic_exchange(&d->activated, true)) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(d->done, STARTED);
    return ESP_OK;
}
esp_err_t echoy_device_stop(echoy_device_handle_t *handle, uint32_t timeout_ms)
{
    if (!handle || !*handle || !timeout_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    struct echoy_device *d = *handle;
    const int64_t deadline = now_us(d) + (int64_t)timeout_ms * 1000;
    atomic_store(&d->accepting, false);
    atomic_store(&d->stopping, true);
    /* Also release workers if UI/acceptance failed before activation. */
    xEventGroupSetBits(d->done, STARTED);
    EventBits_t done =
        xEventGroupWaitBits(d->done, OWNER_DONE | PREPARE_DONE, pdFALSE, pdTRUE, ticks(timeout_ms));
    if ((done & (OWNER_DONE | PREPARE_DONE)) != (OWNER_DONE | PREPARE_DONE)) {
        return ESP_ERR_TIMEOUT;
    }
    /* On SMP, SetBits wakes the waiter before its final event-group access.
     * Wait for both calls to return before freeing their group/context. */
    while (!atomic_load(&d->owner_exited) || !atomic_load(&d->prepare_exited)) {
        if (now_us(d) >= deadline) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    /* Cleanup errors retain contexts for retry, but never retain raw capture.
     * Both producers have exited, so neither can race this erasure. */
    measure_clear_pcm(d);
#endif
    if (d->health) {
        int64_t remaining_ms = (deadline - now_us(d)) / 1000;
        if (remaining_ms <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        esp_err_t error = board_health_stop(&d->health, (uint32_t)remaining_ms);
        if (error != ESP_OK) {
            return error;
        }
    }
    if (d->shutdown_error != ESP_OK) {
        return d->shutdown_error;
    }
    release_context(d);
    *handle = NULL;
    return ESP_OK;
}
