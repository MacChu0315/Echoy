#include "echoy_snapshot_diagnostics.h"
#include "echoy_link_codec.h"
#include "board_touch_trace.h"
#include "esp_heap_caps.h"
#include "esp_iris.h"
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct snapshot_slot {
    echoy_control_view_t view;
    uint8_t encoded[ECHOY_LINK_PLAN_MAX_BYTES];
    size_t size;
    uint64_t token;
};
struct echoy_snapshot_diagnostics {
    bool (*read_control)(void *, echoy_control_view_t *);
    void *ctx;
    bool registered[3], valid;
    atomic_bool ready;
    unsigned active;
    struct snapshot_slot slots[2];
};

/* Iris currently serializes handlers. Still reject overlapping captures,
 * chunk reads and lifecycle changes, without waiting or mixing snapshots.
 * The token sequence survives a stop/start within the same firmware boot. */
static atomic_flag s_busy = ATOMIC_FLAG_INIT;
static uint64_t s_token;

static esp_err_t stop_locked(echoy_snapshot_diagnostics_handle_t *handle)
{
    struct echoy_snapshot_diagnostics *d = *handle;
    atomic_store(&d->ready, false);
    esp_err_t result = ESP_OK;
    for (unsigned method = 1; method <= 3; ++method) {
        if (!d->registered[method - 1]) continue;
        esp_err_t err = esp_iris_rpc_unregister(ECHOY_SNAPSHOT_SERVICE_ID, method);
        /* Iris may already have stopped and removed the registration. Its
         * public API checks executor_busy before reporting NOT_FOUND too. */
        if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) d->registered[method - 1] = false;
        else if (result == ESP_OK) result = err;
    }
    /* Public Iris unregister refuses while its executor can hold user_ctx.
     * Never release storage while even one handler remains registered. */
    if (!d->registered[0] && !d->registered[1] && !d->registered[2]) {
        free(d);
        *handle = NULL;
    }
    return result;
}

static esp_err_t capture(struct echoy_snapshot_diagnostics *d, uint8_t *response,
                         size_t capacity, size_t *size)
{
    if (capacity < ECHOY_SNAPSHOT_METADATA_BYTES) return ESP_ERR_INVALID_SIZE;
    if (s_token == UINT64_MAX) return ESP_ERR_INVALID_STATE;
    struct snapshot_slot *slot = &d->slots[1 - d->active];
    if (!d->read_control(d->ctx, &slot->view)) return ESP_ERR_INVALID_STATE;
    const echoy_control_view_t *v = &slot->view;
    if (!isfinite(v->volume) || v->volume < 0 || v->volume > 1)
        return ESP_ERR_INVALID_STATE;
    slot->size = echoy_link_plan_encode(&v->plan, slot->encoded, sizeof(slot->encoded));
    if (!slot->size) return ESP_ERR_INVALID_STATE;
    slot->token = s_token + 1;
    uint32_t crc = echoy_link_crc32(slot->encoded, slot->size);
    int n = snprintf((char *)response, capacity,
        "{\"schema\":1,\"token\":\"%016" PRIx64 "\",\"size\":%u,\"crc32\":\"%08" PRIx32 "\","
        "\"volume\":%.17g,\"phase\":%d,\"generation\":%" PRIu32 ","
        "\"revision\":%" PRIu32 ",\"committed_revision\":%" PRIu32 ","
        "\"plan_pending\":%s,\"preparing\":%s,\"preparation_failed\":%s,"
        "\"continuous\":%s,\"following\":%s,\"condition\":%d,\"cooperation\":%d,"
        "\"tile\":%d,\"source\":%d,\"clock\":%d,\"state_role\":%d,\"state_phrase\":%u,"
        "\"seed\":%u,\"plan_role\":%d,\"plan_phrase\":%u,\"density\":%.17g,"
        "\"events\":%u,\"source_count\":%u,\"start_us\":%" PRId64 ","
        "\"clock_origin_us\":%" PRId64 ",\"apply_at_us\":%" PRId64 ",\"chunk_max\":%u}",
        slot->token, (unsigned)slot->size, crc, v->volume, v->state.phase,
        v->state.generation, v->state.revision, v->state.committed_revision,
        v->state.plan_pending ? "true" : "false", v->preparing ? "true" : "false",
        v->preparation_failed ? "true" : "false", v->state.continuous ? "true" : "false",
        v->state.following ? "true" : "false", v->condition, v->cooperation,
        v->tile, v->source, v->clock, v->state.role, v->state.phrase,
        (unsigned)v->state.seed, v->plan.role, v->plan.phrase, v->plan.density,
        (unsigned)v->plan.count, (unsigned)v->plan.source_count,
        v->state.start_us, v->state.clock_origin_us, v->state.apply_at_us,
        ECHOY_SNAPSHOT_CHUNK_BYTES);
    if (n < 0 || (size_t)n >= capacity) return ESP_ERR_INVALID_SIZE;
    s_token = slot->token;
    d->active = 1 - d->active;
    d->valid = true;
    *size = (size_t)n;
    return ESP_OK;
}

static uint64_t unsigned_le(const uint8_t *bytes, unsigned count)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= (uint64_t)bytes[i] << (i * 8);
    return value;
}

struct touch_snapshot {
    board_touch_trace_summary_t summary;
    board_touch_trace_event_t events[BOARD_TOUCH_TRACE_CAPACITY];
    board_touch_irq_event_t irqs[BOARD_TOUCH_IRQ_CAPACITY];
    unsigned latest[16], presses;
};

static bool append_json(uint8_t *out, size_t capacity, size_t *at, const char *format, ...)
{
    if (*at >= capacity) return false;
    va_list args;
    va_start(args, format);
    int n = vsnprintf((char *)out + *at, capacity - *at, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= capacity - *at) return false;
    *at += (size_t)n;
    return true;
}

static esp_err_t touch_unavailable(uint8_t *out, size_t capacity, size_t *size, const char *reason)
{
    size_t at = 0;
    if (!append_json(out, capacity, &at,
        "{\"schema\":\"echoy-touch-v1\",\"available\":false,\"reason\":\"%s\"}", reason))
        return ESP_ERR_INVALID_SIZE;
    *size = at;
    return ESP_OK;
}

static bool format_touch(const struct touch_snapshot *t, unsigned count,
                         uint8_t *out, size_t capacity, size_t *size)
{
    const board_touch_trace_summary_t *s = &t->summary;
    size_t at = 0;
    if (!append_json(out, capacity, &at,
        "{\"schema\":\"echoy-touch-v1\",\"available\":true,\"state\":%" PRIu32 ",\"summary\":["
        "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ","
        "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ","
        "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ","
        "%" PRIu32 ",%" PRIu32 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 "],"
        "\"pressed_retained\":%u,\"pressed_omitted\":%" PRIu32 ","
        "\"events_not_returned\":%" PRIu32 ",\"irqs_not_returned\":%" PRIu32 ",\"edges\":[",
        s->state, s->irq_count, s->irq_overwritten, s->reads, s->read_errors, s->raw_reports,
        s->raw_presses, s->raw_releases, s->observers, s->observer_presses,
        s->submitted, s->same_grid, s->multiple_irq_windows, s->irq_during_read,
        s->event_count, s->event_overwritten, s->busy_drops, s->events_copied,
        s->events_omitted, s->irqs_copied, s->irqs_omitted, s->max_read_gap_us,
        s->max_read_duration_us, s->max_latest_irq_age_us, s->max_raw_to_observer_us,
        t->presses, s->observer_presses - count, s->events_copied - count, s->irqs_copied))
        return false;
    for (unsigned i = 0; i < count; ++i) {
        const board_touch_trace_event_t *e = &t->events[t->latest[(t->presses - count + i) % 16]];
        if (!append_json(out, capacity, &at,
            "%s[%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ","
            "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRId32 ",%d,%u,%u,%u,%u]",
            i ? "," : "", e->at_us, e->irq_at_us, e->raw_at_us, e->capture_at_us,
            e->seq, e->read_seq, e->raw_seq, e->irq_seq, e->phase, (int)e->grid,
            (unsigned)e->pending_seed, (unsigned)e->submitted, (unsigned)e->raw_match,
            (unsigned)e->same_grid)) return false;
    }
    if (!append_json(out, capacity, &at, "]}")) return false;
    *size = at;
    return true;
}

static esp_err_t capture_touch(uint8_t *response, size_t capacity, size_t *size)
{
    if (capacity < 128) return ESP_ERR_INVALID_SIZE;
    if (capacity > ECHOY_TOUCH_SNAPSHOT_BYTES) capacity = ECHOY_TOUCH_SNAPSHOT_BYTES;
    struct touch_snapshot *t = heap_caps_calloc(1, sizeof(*t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!t) return touch_unavailable(response, capacity, size, "no_psram");
    esp_err_t result;
    if (!board_touch_trace_snapshot(&t->summary, t->events, BOARD_TOUCH_TRACE_CAPACITY,
                                    t->irqs, BOARD_TOUCH_IRQ_CAPACITY)) {
        result = touch_unavailable(response, capacity, size, "inactive_or_busy");
        goto done;
    }
    if (t->summary.events_copied > BOARD_TOUCH_TRACE_CAPACITY ||
        t->summary.irqs_copied > BOARD_TOUCH_IRQ_CAPACITY) {
        result = touch_unavailable(response, capacity, size, "invalid_summary");
        goto done;
    }
    for (uint32_t i = 0; i < t->summary.events_copied; ++i) {
        const board_touch_trace_event_t *e = &t->events[i];
        if (e->kind == BOARD_TOUCH_TRACE_OBSERVER && e->pressed && e->edge == 1)
            t->latest[t->presses++ % 16] = i;
    }
    if (t->summary.observer_presses < t->presses) {
        result = touch_unavailable(response, capacity, size, "invalid_summary");
        goto done;
    }
    result = ESP_ERR_INVALID_SIZE;
    /* Retry formatting the same immutable snapshot, never reread input. Keep
     * the latest rows that fit and account for every omitted row explicitly. */
    unsigned count = t->presses < 16 ? t->presses : 16;
    for (;;) {
        if (format_touch(t, count, response, capacity, size)) { result = ESP_OK; break; }
        if (!count) break;
        --count;
    }
done:
    free(t);
    return result;
}

static esp_err_t snapshot_rpc(const esp_iris_rpc_request_t *request, uint8_t *response,
                              size_t capacity, size_t *size, void *ctx)
{
    if (size) *size = 0;
    if (!request || !response || !size || !ctx) return ESP_ERR_INVALID_ARG;
    if (atomic_flag_test_and_set(&s_busy)) return ESP_ERR_INVALID_STATE;
    struct echoy_snapshot_diagnostics *d = ctx;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!atomic_load(&d->ready)) goto done;
    if (request->service_id != ECHOY_SNAPSHOT_SERVICE_ID) {
        err = ESP_ERR_NOT_SUPPORTED;
    } else if (request->method_id == 1) {
        err = request->payload_size ? ESP_ERR_INVALID_SIZE : capture(d, response, capacity, size);
    } else if (request->method_id == 2) {
        err = ESP_ERR_INVALID_SIZE;
        if (request->payload_size != 14 || !request->payload) goto done;
        const struct snapshot_slot *slot = &d->slots[d->active];
        uint64_t token = unsigned_le(request->payload, 8);
        size_t offset = (size_t)unsigned_le(request->payload + 8, 4);
        size_t length = (size_t)unsigned_le(request->payload + 12, 2);
        if (!d->valid || token != slot->token) { err = ESP_ERR_INVALID_STATE; goto done; }
        if (!length || length > ECHOY_SNAPSHOT_CHUNK_BYTES || length > capacity ||
            offset > slot->size || length > slot->size - offset) goto done;
        memcpy(response, slot->encoded + offset, length);
        *size = length;
        err = ESP_OK;
    } else if (request->method_id == 3) {
        err = request->payload_size ? ESP_ERR_INVALID_SIZE : capture_touch(response, capacity, size);
    } else {
        err = ESP_ERR_NOT_SUPPORTED;
    }
done:
    atomic_flag_clear(&s_busy);
    return err;
}

esp_err_t echoy_snapshot_diagnostics_start(const echoy_platform_t *platform,
                                          echoy_snapshot_diagnostics_handle_t *out)
{
    if (!platform || !platform->read_control || !out || *out) return ESP_ERR_INVALID_ARG;
    if (atomic_flag_test_and_set(&s_busy)) return ESP_ERR_INVALID_STATE;
    struct echoy_snapshot_diagnostics *d = heap_caps_calloc(
        1, sizeof(*d), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t result = ESP_ERR_NO_MEM;
    if (!d) goto done;
    atomic_init(&d->ready, false);
    d->read_control = platform->read_control;
    d->ctx = platform->ctx;
    *out = d;
    for (unsigned method = 1; method <= 3; ++method) {
        result = esp_iris_rpc_register(ECHOY_SNAPSHOT_SERVICE_ID, method, snapshot_rpc, d);
        if (result != ESP_OK) {
            (void)stop_locked(out); /* Retained non-NULL handle documents busy cleanup. */
            goto done;
        }
        d->registered[method - 1] = true;
    }
    atomic_store(&d->ready, true);
done:
    atomic_flag_clear(&s_busy);
    return result;
}

esp_err_t echoy_snapshot_diagnostics_stop(echoy_snapshot_diagnostics_handle_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (!*handle) return ESP_OK;
    atomic_store(&(*handle)->ready, false);
    if (atomic_flag_test_and_set(&s_busy)) return ESP_ERR_INVALID_STATE;
    esp_err_t result = stop_locked(handle);
    atomic_flag_clear(&s_busy);
    return result;
}
