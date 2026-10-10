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

#define BLOCK_FRAMES 240u
#define QUEUE_US                                                                                   \
    ((int64_t)BOARD_AUDIO_DMA_FRAMES * BOARD_AUDIO_DMA_DESCRIPTORS * 1000000 /                     \
     BOARD_AUDIO_SAMPLE_RATE)
#define OWNER_DONE BIT0
#define PREPARE_DONE BIT1
#define STARTED BIT2
#define COMMAND_COUNT 32
#define DIAGNOSTIC_BYTES 512
static const char *TAG = "echoy_device";

_Static_assert(BOARD_AUDIO_SAMPLE_RATE == 24000, "PCM limiter requires 24 kHz");

typedef struct {
    unsigned probe_command; /* 0 UI, 1 reference tone, 2 cancel diagnostic. */
    unsigned probe_generation;
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
    int16_t pcm[BLOCK_FRAMES];
} pcm_block_t;
struct echoy_device {
    QueueHandle_t commands, startup, jobs, prepared, diagnostics;
    SemaphoreHandle_t view_lock;
    EventGroupHandle_t done;
    TaskHandle_t owner_task, prepare_task;
    board_health_handle_t health;
    echoy_band_t *band; /* Exclusively owned by audio/control after startup handoff. */
    echoy_control_view_t cache, staging;
    pcm_block_t *block;
    atomic_bool stopping, queue_fault, accepting, owner_exited, prepare_exited, activated;
    bool audio_fault, fatal, preparation_in_flight, battery_empty;
    echoy_condition_t condition;
    int64_t origin_us, next_log_us, last_prepare_us, max_render_us;
    uint64_t accepted_frames;
    echoy_device_pcm_t pcm_processor;
    uint64_t pcm_clipped, pcm_limited;
    unsigned deadline_misses, rejected_actions, queue_overflows;
    esp_err_t shutdown_error;
    board_audio_stats_t last_audio_stats;
    atomic_bool probe_busy;
    atomic_uint probe_sequence;
    bool probe_pending, probe_active;
    size_t probe_position;
    echoy_device_diagnostics_t probe, probe_cache;
};

static int64_t now_us(void *ctx)
{
    (void)ctx;
    return esp_timer_get_time();
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
    const command_t command = {.action = action, .value = value, .at = at};
    if (xQueueSend(d->commands, &command, 0) != pdTRUE) {
        /* Do not lose Cancel and later execute an older Record. Fail silent,
         * discard all queued actions, and require explicit recovery. */
        atomic_store(&d->queue_fault, true);
        return false;
    }
    return true;
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
        xSemaphoreGive(d->view_lock);
        if (!ready || atomic_exchange(&d->probe_busy, true)) {
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
    if (d->band) {
        echoy_band_read(d->band, ECHOY_TILE_A, &d->staging);
    }
    board_health_snapshot_t health;
    bool known = d->health && board_health_get(d->health, &health) == ESP_OK && health.valid;
    d->staging.battery_unknown = !known;
    d->staging.battery = known ? (unsigned)health.percentage : 0;
    d->staging.imu_unknown = true;
    d->staging.condition = d->condition;
    d->staging.state.fault = d->condition != ECHOY_NORMAL;
    xSemaphoreTake(d->view_lock, portMAX_DELAY);
    d->cache = d->staging;
    d->probe_cache = d->probe;
    xSemaphoreGive(d->view_lock);
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
    if (atomic_load(&d->probe_busy)) {
        finish_probe(d, ECHOY_PROBE_FAILED, error);
    }
    set_condition(d, ECHOY_AUDIO_FAULT);
    if (*audio) {
        board_audio_get_stats(*audio, &d->last_audio_stats);
    }
    const esp_err_t close_error = board_audio_close(audio);
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
    d->last_audio_stats = (board_audio_stats_t){0};
    d->accepted_frames = 0;
    /* I2S owns three 5 ms DMA descriptors. This is an estimated presentation
     * timeline, not a measured DAC playhead. Hardware acceptance measures it. */
    d->origin_us = now_us(d) + QUEUE_US;
    return true;
}
static void prepare_worker(void *arg)
{
    struct echoy_device *d = arg;
    xEventGroupWaitBits(d->done, STARTED, pdFALSE, pdTRUE, portMAX_DELAY);
    echoy_band_t *band = atomic_load(&d->stopping) ? NULL : echoy_band_create();
    xQueueSend(d->startup, &band, portMAX_DELAY);
    ESP_LOGI(TAG, "musical role gain milli: pulse=%u voice=%u dust=%u harmony=%u",
             (unsigned)(echoy_device_role_gains[0]*1000), (unsigned)(echoy_device_role_gains[1]*1000),
             (unsigned)(echoy_device_role_gains[2]*1000), (unsigned)(echoy_device_role_gains[3]*1000));
    while (!atomic_load(&d->stopping)) {
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
        if (result.ok) {
            result.ok = echoy_band_calibrate_job(job, echoy_device_role_gains);
            result.calibration_failed = !result.ok;
        }
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
            ++d->rejected_actions;
            continue;
        }
        /* Preserve input timestamps; the shared model already provides the
         * approved 60 ms lead before the first count-in cue. */
        bool accepted =
            echoy_band_action(d->band, ECHOY_TILE_A, command.action, command.value, command.at);
        if (!accepted) {
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
             "overflow=%u limited=%llu",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (long long)d->last_prepare_us,
             (long long)d->max_render_us, d->rejected_actions, d->queue_overflows,
             (unsigned long long)d->pcm_limited);
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
        update_health(d);
        bool flush = process_commands(d, &audio);
        echoy_condition_t desired = d->audio_fault     ? ECHOY_AUDIO_FAULT
                                    : d->battery_empty ? ECHOY_BATTERY_EMPTY
                                                       : ECHOY_NORMAL;
        flush |= desired != d->condition;
        set_condition(d, desired);
        if (desired != ECHOY_NORMAL && atomic_load(&d->probe_busy)) {
            finish_probe(d, ECHOY_PROBE_FAILED, ESP_ERR_INVALID_STATE);
        }
        if (flush) {
            echoy_device_pcm_reset(&d->pcm_processor);
        }
        if (flush && audio) {
            esp_err_t error = board_audio_flush(audio);
            if (error != ESP_OK) {
                fail_audio(d, &audio, "clear queued sound", error);
            } else {
                d->accepted_frames = 0;
                d->origin_us = now_us(d) + QUEUE_US;
            }
        }
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
            d->preparation_in_flight = false;
            d->last_prepare_us = result.elapsed_us;
            if (result.calibration_failed) {
                echoy_band_discard_job(result.job);
                fail_audio(d, &audio, "musical output calibration", ESP_ERR_INVALID_RESPONSE);
            } else {
                echoy_band_finish_job(d->band, result.job, now_us(d));
            }
        }
        if (d->band) {
            echoy_band_tick(d->band, now_us(d));
            if (!d->preparation_in_flight) {
                echoy_band_job_t *job = echoy_band_take_job(d->band);
                if (job) {
                    if (xQueueSend(d->jobs, &job, 0) == pdTRUE) {
                        d->preparation_in_flight = true;
                    } else {
                        echoy_band_discard_job(job);
                        fail_audio(d, &audio, "preparation queue", ESP_ERR_INVALID_STATE);
                    }
                }
            }
        }
        publish(d);
        log_resources(d, audio);
        if (!audio || d->condition != ECHOY_NORMAL || !d->band) {
            vTaskDelay(ticks(10));
            continue;
        }
        int64_t at =
            d->origin_us + (int64_t)(d->accepted_frames * 1000000 / BOARD_AUDIO_SAMPLE_RATE);
        int64_t start = now_us(d);
        if (start > at) {
            /* Do not replay stale samples to hide a missed output deadline. */
            ++d->deadline_misses;
            fail_audio(d, &audio, "estimated output deadline", ESP_ERR_TIMEOUT);
            continue;
        }
        if (d->probe_active) {
            echoy_audio_probe_render(d->probe_position, d->block->pcm, BLOCK_FRAMES);
        } else {
            echoy_band_render(d->band, at, d->block->floating, BLOCK_FRAMES);
            echoy_device_pcm_stats_t stats;
            if (!echoy_device_pcm_convert(&d->pcm_processor, d->block->floating, d->block->pcm,
                                          BLOCK_FRAMES, &stats)) {
                fail_audio(d, &audio, "nonfinite PCM", ESP_ERR_INVALID_RESPONSE);
            } else {
                d->pcm_clipped += stats.clipped;
                d->pcm_limited += stats.limited;
            }
        }
        if (!audio) {
            continue;
        }
        int64_t elapsed = now_us(d) - start;
        if (elapsed > d->max_render_us) {
            d->max_render_us = elapsed;
        }
        size_t written = 0;
        esp_err_t error = board_audio_write(audio, d->block->pcm, BLOCK_FRAMES, &written);
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
    if (d->view_lock) {
        vSemaphoreDelete(d->view_lock);
    }
    if (d->done) {
        vEventGroupDelete(d->done);
    }
    free(d->block);
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
    d->condition = ECHOY_LOADING;
    d->cache = (echoy_control_view_t){
        .memories = {1, 1, 1, 2},
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
    d->view_lock = xSemaphoreCreateMutex();
    d->done = xEventGroupCreate();
    d->block = heap_caps_calloc(1, sizeof(*d->block), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!d->commands || !d->startup || !d->jobs || !d->prepared || !d->diagnostics ||
        !d->view_lock || !d->done || !d->block) {
        release_context(d);
        return ESP_ERR_NO_MEM;
    }
    /* Tasks start suspended by a gate until both handles are available. */
    if (xTaskCreate(prepare_worker, "echoy_prepare", 12288, d, 1, &d->prepare_task) != pdPASS) {
        release_context(d);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(owner_worker, "echoy_audio", 12288, d, 5, &d->owner_task) != pdPASS) {
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
