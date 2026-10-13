#include "board_touch_trace.h"

#ifdef ESP_PLATFORM
#include "echoy_model.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Touch diagnostics need lock-free task counters");

/* ISR touches this INTERNAL static object only. No PSRAM, I/O, allocations,
 * task notification or logs here; GSP keeps ownership of the actual IRQ. */
static DRAM_ATTR struct {
    portMUX_TYPE lock;
    bool enabled;
    uint32_t count;
    int64_t latest_us;
    board_touch_irq_event_t events[BOARD_TOUCH_IRQ_CAPACITY];
} irq = { .lock = portMUX_INITIALIZER_UNLOCKED };

static struct {
    esp_lcd_touch_handle_t touch;
    esp_err_t (*read)(esp_lcd_touch_handle_t);
    bool (*get)(esp_lcd_touch_handle_t, uint16_t *, uint16_t *, uint16_t *, uint8_t *, uint8_t);
    board_touch_trace_event_t *events; /* PSRAM; never accessed by ISR. */
    board_touch_trace_summary_t summary;
    uint32_t previous_irq, last_raw_seq, generation;
    int64_t previous_read_us, last_raw_us, grid_capture_us;
    uint16_t submitted_grids;
    bool raw_known, raw_pressed, observer_pressed;
} trace;
static atomic_flag task_guard = ATOMIC_FLAG_INIT;
static atomic_uint busy_drops, callbacks;
enum { CONTROL_RESET = 1, CONTROL_CAPTURE = 2, CONTROL_FREEZE = 4 };
static atomic_uint pending_control;
/* Odd while a reset is changing task/IRQ state, even only after it completes.
 * A consumed pending flag alone cannot identify a completed reset. */
static atomic_uint reset_epoch;
static atomic_bool recording_enabled, capture_seen;
/* Snapshot scratch is also INTERNAL even if its caller has a PSRAM stack.
 * Serialized by task_guard, and only the IRQ-ring copy holds irq.lock. */
static DRAM_ATTR board_touch_irq_event_t irq_copy[BOARD_TOUCH_IRQ_CAPACITY];

static bool task_enter(void)
{
    return !atomic_flag_test_and_set_explicit(&task_guard, memory_order_acquire);
}
static void task_leave(void)
{
    atomic_flag_clear_explicit(&task_guard, memory_order_release);
}
static bool writer_enter(void)
{
    if (task_enter()) return true;
    if (atomic_load_explicit(&recording_enabled, memory_order_relaxed))
        atomic_fetch_add_explicit(&busy_drops, 1, memory_order_relaxed);
    return false;
}
static void irq_status(uint32_t *seq, int64_t *at)
{
    portENTER_CRITICAL(&irq.lock);
    *seq = irq.count;
    *at = irq.latest_us;
    portEXIT_CRITICAL(&irq.lock);
}
static void append(board_touch_trace_event_t event)
{
    uint32_t index = trace.summary.event_count++;
    event.seq = index + 1;
    trace.events[index % BOARD_TOUCH_TRACE_CAPACITY] = event;
    if (index >= BOARD_TOUCH_TRACE_CAPACITY) ++trace.summary.event_overwritten;
}
static void maximum(int64_t *value, int64_t candidate)
{
    if (candidate > *value) *value = candidate;
}

static void reset_locked(void);
static void apply_control_locked(void)
{
    for (;;) {
        unsigned control = atomic_exchange_explicit(&pending_control, 0, memory_order_acquire);
        if (!trace.touch || !control) return;
        if (control & CONTROL_RESET) {
            atomic_fetch_add_explicit(&reset_epoch, 1, memory_order_acq_rel);
            reset_locked();
            trace.generation = atomic_fetch_add_explicit(&reset_epoch, 1, memory_order_release) + 1;
            trace.summary.state = BOARD_TOUCH_TRACE_ARMED;
        }
        if ((control & CONTROL_CAPTURE) && trace.summary.state == BOARD_TOUCH_TRACE_ARMED)
            trace.summary.state = BOARD_TOUCH_TRACE_CAPTURING;
        if ((control & CONTROL_FREEZE) && trace.summary.state != BOARD_TOUCH_TRACE_INACTIVE)
            trace.summary.state = BOARD_TOUCH_TRACE_FROZEN;
        bool enabled = trace.summary.state == BOARD_TOUCH_TRACE_ARMED ||
                       trace.summary.state == BOARD_TOUCH_TRACE_CAPTURING;
        atomic_store_explicit(&recording_enabled, enabled, memory_order_release);
        portENTER_CRITICAL(&irq.lock);
        irq.enabled = enabled;
        portEXIT_CRITICAL(&irq.lock);
        /* A control request can arrive while the IRQ lock is held. Consume it
         * before publishing this state; UI control calls never wait on us. */
    }
}

static bool recording_locked(void)
{
    apply_control_locked();
    return trace.summary.state == BOARD_TOUCH_TRACE_ARMED ||
           trace.summary.state == BOARD_TOUCH_TRACE_CAPTURING;
}

static esp_err_t read_trace(esp_lcd_touch_handle_t touch)
{
    atomic_fetch_add_explicit(&callbacks, 1, memory_order_relaxed);
    uint32_t generation = atomic_load_explicit(&reset_epoch, memory_order_acquire);
    bool reset_pending = (generation & 1u) ||
        (atomic_load_explicit(&pending_control, memory_order_acquire) & CONTROL_RESET) != 0;
    board_touch_trace_event_t event = { .kind = BOARD_TOUCH_TRACE_READ, .grid = -1 };
    irq_status(&event.irq_seq, &event.irq_at_us);
    event.at_us = esp_timer_get_time();
    /* Call the physical handle, not a shadow object: CST9220 embeds it. */
    esp_err_t result = trace.read(touch);
    event.end_us = esp_timer_get_time();
    int64_t unused;
    irq_status(&event.irq_end_seq, &unused);
    event.result = result;
    if (writer_enter()) {
        if (!recording_locked()) { task_leave(); goto done; }
        if (reset_pending || generation != trace.generation) {
            atomic_fetch_add_explicit(&busy_drops, 1, memory_order_relaxed);
            task_leave();
            goto done;
        }
        event.read_seq = ++trace.summary.reads;
        if (result != ESP_OK) ++trace.summary.read_errors;
        if (event.irq_seq - trace.previous_irq > 1) ++trace.summary.multiple_irq_windows;
        trace.previous_irq = event.irq_seq;
        trace.summary.irq_during_read += event.irq_end_seq - event.irq_seq;
        if (trace.summary.reads > 1)
            maximum(&trace.summary.max_read_gap_us, event.at_us - trace.previous_read_us);
        trace.previous_read_us = event.at_us;
        maximum(&trace.summary.max_read_duration_us, event.end_us - event.at_us);
        if (event.irq_seq)
            maximum(&trace.summary.max_latest_irq_age_us, event.at_us - event.irq_at_us);
        append(event);
        task_leave();
    }
done:
    atomic_fetch_sub_explicit(&callbacks, 1, memory_order_relaxed);
    return result;
}

static bool get_trace(esp_lcd_touch_handle_t touch, uint16_t *x, uint16_t *y,
                      uint16_t *strength, uint8_t *points, uint8_t capacity)
{
    atomic_fetch_add_explicit(&callbacks, 1, memory_order_relaxed);
    uint32_t generation = atomic_load_explicit(&reset_epoch, memory_order_acquire);
    bool reset_pending = (generation & 1u) ||
        (atomic_load_explicit(&pending_control, memory_order_acquire) & CONTROL_RESET) != 0;
    bool pressed = trace.get(touch, x, y, strength, points, capacity);
    int64_t at = esp_timer_get_time();
    if (writer_enter()) {
        if (!recording_locked()) { task_leave(); goto done; }
        if (reset_pending || generation != trace.generation) {
            atomic_fetch_add_explicit(&busy_drops, 1, memory_order_relaxed);
            task_leave();
            goto done;
        }
        board_touch_trace_event_t event = {
            .kind = BOARD_TOUCH_TRACE_RAW, .grid = -1, .at_us = at,
            .read_seq = trace.summary.reads, .raw_seq = ++trace.summary.raw_reports,
            .result = pressed, .pressed = pressed
        };
        /* Do not inspect arrays on false/error. Forward even invalid arguments
         * unchanged; only valid calls can establish a returned raw edge. */
        bool valid = x && y && points && capacity;
        if (pressed && valid) {
            event.points = *points;
            if (*points) { event.x = x[0]; event.y = y[0]; }
        }
        if (valid) {
            event.edge = !trace.raw_known ? 2 : trace.raw_pressed == pressed ? 0 : pressed ? 1 : -1;
            if (event.edge == 1) ++trace.summary.raw_presses;
            if (event.edge == -1) ++trace.summary.raw_releases;
            trace.raw_known = true;
            trace.raw_pressed = pressed;
            trace.last_raw_seq = event.raw_seq;
            trace.last_raw_us = at;
        }
        irq_status(&event.irq_seq, &event.irq_at_us);
        append(event);
        task_leave();
    }
done:
    atomic_fetch_sub_explicit(&callbacks, 1, memory_order_relaxed);
    return pressed;
}

static void reset_locked(void)
{
    trace.summary = (board_touch_trace_summary_t){0};
    trace.previous_irq = trace.last_raw_seq = 0;
    trace.previous_read_us = trace.last_raw_us = trace.grid_capture_us = 0;
    trace.submitted_grids = 0;
    trace.raw_known = trace.raw_pressed = trace.observer_pressed = false;
    atomic_store_explicit(&busy_drops, 0, memory_order_relaxed);
    portENTER_CRITICAL(&irq.lock);
    irq.count = 0;
    irq.latest_us = 0;
    portEXIT_CRITICAL(&irq.lock);
}

esp_err_t board_touch_trace_attach(esp_lcd_touch_handle_t touch)
{
    if (!touch || !touch->read_data || !touch->get_xy) return ESP_ERR_INVALID_ARG;
    if (!task_enter()) return ESP_ERR_INVALID_STATE;
    if (trace.touch) { task_leave(); return ESP_ERR_INVALID_STATE; }
    board_touch_trace_event_t *events = heap_caps_calloc(BOARD_TOUCH_TRACE_CAPACITY,
        sizeof(*events), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!events) { task_leave(); return ESP_ERR_NO_MEM; }
    trace.touch = touch;
    trace.read = touch->read_data;
    trace.get = touch->get_xy;
    trace.events = events;
    reset_locked();
    atomic_store(&pending_control, 0);
    trace.generation = atomic_load(&reset_epoch);
    atomic_store(&recording_enabled, false);
    atomic_store(&capture_seen, false);
    touch->read_data = read_trace;
    touch->get_xy = get_trace;
    portENTER_CRITICAL(&irq.lock);
    irq.enabled = false;
    portEXIT_CRITICAL(&irq.lock);
    task_leave();
    return ESP_OK;
}

esp_err_t board_touch_trace_detach(void)
{
    if (!task_enter()) return ESP_ERR_INVALID_STATE;
    if (!trace.touch) { task_leave(); return ESP_OK; }
    if (atomic_load_explicit(&callbacks, memory_order_relaxed) ||
        trace.touch->read_data != read_trace || trace.touch->get_xy != get_trace) {
        task_leave();
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&irq.lock);
    irq.enabled = false;
    portEXIT_CRITICAL(&irq.lock);
    trace.touch->read_data = trace.read;
    trace.touch->get_xy = trace.get;
    heap_caps_free(trace.events);
    atomic_store(&pending_control, 0);
    atomic_store(&recording_enabled, false);
    atomic_store(&capture_seen, false);
    memset(&trace, 0, sizeof(trace));
    task_leave();
    return ESP_OK;
}

void IRAM_ATTR board_touch_trace_wake_from_isr(void *unused)
{
    (void)unused;
    int64_t at = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&irq.lock);
    if (irq.enabled) {
        uint32_t index = irq.count++;
        irq.latest_us = at;
        irq.events[index % BOARD_TOUCH_IRQ_CAPACITY] =
            (board_touch_irq_event_t){ .seq = index + 1, .at_us = at };
    }
    portEXIT_CRITICAL_ISR(&irq.lock);
}

void board_touch_trace_reset(void)
{
    atomic_store(&capture_seen, false);
    atomic_store_explicit(&pending_control, CONTROL_RESET, memory_order_release);
    if (!writer_enter()) return;
    apply_control_locked();
    task_leave();
}

void board_touch_trace_note_phase(int phase)
{
    bool capturing = phase == ECHOY_COUNT_IN || phase == ECHOY_RECORDING;
    if (capturing) {
        if (atomic_exchange(&capture_seen, true)) return;
        atomic_fetch_or_explicit(&pending_control, CONTROL_CAPTURE, memory_order_release);
        if (!writer_enter()) return;
        apply_control_locked();
        task_leave();
    } else if (atomic_exchange(&capture_seen, false)) {
        board_touch_trace_freeze();
    }
}

void board_touch_trace_freeze(void)
{
    /* Stop IRQ collection immediately even while a task snapshot is copying.
     * Pending control is consumed by the next writer/snapshot, so a one-shot
     * Cancel cannot be lost to diagnostic contention. */
    atomic_store_explicit(&recording_enabled, false, memory_order_release);
    portENTER_CRITICAL(&irq.lock);
    irq.enabled = false;
    portEXIT_CRITICAL(&irq.lock);
    atomic_fetch_or_explicit(&pending_control, CONTROL_FREEZE, memory_order_release);
    if (!writer_enter()) return;
    apply_control_locked();
    task_leave();
}

void board_touch_trace_note_observer(int x, int y, bool pressed, int64_t at, int phase,
                                    int64_t capture, uint16_t seed, bool submitted)
{
    if (!writer_enter()) return;
    if (!trace.touch) { task_leave(); return; }
    if (!recording_locked()) { task_leave(); return; }
    board_touch_trace_event_t event = {
        .kind = BOARD_TOUCH_TRACE_OBSERVER, .at_us = at, .x = x, .y = y,
        .pressed = pressed, .phase = phase, .capture_at_us = capture,
        .pending_seed = seed, .submitted = submitted, .grid = -1,
        .read_seq = trace.summary.reads, .raw_seq = trace.last_raw_seq,
        .raw_at_us = trace.last_raw_us,
        .raw_match = trace.raw_known && trace.raw_pressed == pressed,
        .edge = trace.observer_pressed == pressed ? 0 : pressed ? 1 : -1
    };
    ++trace.summary.observers;
    if (event.edge == 1) ++trace.summary.observer_presses;
    trace.observer_pressed = pressed;
    if (submitted) ++trace.summary.submitted;
    if (event.raw_match) maximum(&trace.summary.max_raw_to_observer_us, at - trace.last_raw_us);
    if (capture != trace.grid_capture_us) {
        trace.grid_capture_us = capture;
        trace.submitted_grids = 0;
    }
    int64_t relative = at - capture;
    if (submitted && pressed && capture > 0 &&
        (phase == ECHOY_COUNT_IN || phase == ECHOY_RECORDING) &&
        relative >= 0 && relative < INT64_C(2500000)) {
        int step = (int)((relative + 78125) / 156250);
        if (step > 15) step = 15;
        event.grid = (int8_t)step;
        uint16_t bit = (uint16_t)(1u << step);
        event.same_grid = (trace.submitted_grids & bit) != 0;
        if (event.same_grid) ++trace.summary.same_grid;
        trace.submitted_grids |= bit;
    }
    irq_status(&event.irq_seq, &event.irq_at_us);
    append(event);
    task_leave();
}

bool board_touch_trace_snapshot(board_touch_trace_summary_t *summary,
                                board_touch_trace_event_t *events, size_t event_capacity,
                                board_touch_irq_event_t *irqs, size_t irq_capacity)
{
    if (!summary || (event_capacity && !events) || (irq_capacity && !irqs)) return false;
    if (!task_enter()) return false;
    if (!trace.touch) { task_leave(); return false; }
    apply_control_locked();
    portENTER_CRITICAL(&irq.lock);
    uint32_t irq_count = irq.count;
    size_t irq_retained = irq_count < BOARD_TOUCH_IRQ_CAPACITY ? irq_count : BOARD_TOUCH_IRQ_CAPACITY;
    size_t irq_copied = irq_capacity < irq_retained ? irq_capacity : irq_retained;
    /* Only INTERNAL writes with interrupts held; PSRAM copy happens after. */
    for (size_t n = 0; n < irq_copied; ++n)
        irq_copy[n] = irq.events[(irq_count - irq_copied + n) % BOARD_TOUCH_IRQ_CAPACITY];
    portEXIT_CRITICAL(&irq.lock);
    *summary = trace.summary;
    summary->busy_drops = atomic_load_explicit(&busy_drops, memory_order_relaxed);
    summary->irq_count = irq_count;
    summary->irq_overwritten = irq_count - irq_retained;
    summary->irqs_copied = irq_copied;
    summary->irqs_omitted = irq_retained - irq_copied;
    uint32_t count = summary->event_count;
    size_t retained = count < BOARD_TOUCH_TRACE_CAPACITY ? count : BOARD_TOUCH_TRACE_CAPACITY;
    size_t copied = event_capacity < retained ? event_capacity : retained;
    summary->events_copied = copied;
    summary->events_omitted = retained - copied;
    for (size_t n = 0; n < copied; ++n)
        events[n] = trace.events[(count - copied + n) % BOARD_TOUCH_TRACE_CAPACITY];
    if (irq_copied) memcpy(irqs, irq_copy, irq_copied * sizeof(*irqs));
    task_leave();
    return true;
}

void board_touch_trace_dump(void)
{
    board_touch_trace_event_t *events = heap_caps_calloc(BOARD_TOUCH_TRACE_CAPACITY,
        sizeof(*events), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!events) { puts("echoy-touch snapshot unavailable: no PSRAM"); return; }
    board_touch_trace_summary_t s;
    if (!board_touch_trace_snapshot(&s, events, BOARD_TOUCH_TRACE_CAPACITY, NULL, 0)) {
        heap_caps_free(events);
        puts("echoy-touch snapshot unavailable: inactive/busy");
        return;
    }
    printf("echoy-touch irq=%" PRIu32 " irq_overwritten=%" PRIu32
           " reads=%" PRIu32 " read_errors=%" PRIu32 " raw=%" PRIu32
           " raw_edges=%" PRIu32 "/%" PRIu32 " observers=%" PRIu32
           " observer_presses=%" PRIu32 " submitted=%" PRIu32 " same_grid=%" PRIu32 "\n",
           s.irq_count, s.irq_overwritten, s.reads, s.read_errors, s.raw_reports,
           s.raw_presses, s.raw_releases, s.observers, s.observer_presses, s.submitted, s.same_grid);
    printf("echoy-touch multi_irq_windows=%" PRIu32 " irq_during_read=%" PRIu32
           " max_read_gap_us=%" PRId64 " max_read_us=%" PRId64
           " max_latest_irq_age_us=%" PRId64 " max_raw_observer_us=%" PRId64
           " overwritten=%" PRIu32 " busy_drops=%" PRIu32 "\n",
           s.multiple_irq_windows, s.irq_during_read, s.max_read_gap_us, s.max_read_duration_us,
           s.max_latest_irq_age_us, s.max_raw_to_observer_us, s.event_overwritten, s.busy_drops);
    unsigned significant = 0, printed = 0;
    for (uint32_t n = 0; n < s.events_copied; ++n)
        if (events[n].kind == BOARD_TOUCH_TRACE_OBSERVER ||
            (events[n].kind == BOARD_TOUCH_TRACE_RAW && events[n].edge)) ++significant;
    unsigned skip = significant > 64 ? significant - 64 : 0;
    for (uint32_t n = 0; n < s.events_copied; ++n) {
        board_touch_trace_event_t *e = &events[n];
        if (e->kind != BOARD_TOUCH_TRACE_OBSERVER &&
            !(e->kind == BOARD_TOUCH_TRACE_RAW && e->edge)) continue;
        if (skip) { --skip; continue; }
        printf("echoy-touch seq=%" PRIu32 " kind=%s at=%" PRId64
               " read=%" PRIu32 " raw=%" PRIu32 " irq=%" PRIu32 " irq_at=%" PRId64
               " xy=%" PRId32 ",%" PRId32 " down=%d edge=%d raw_at=%" PRId64
               " matched=%d phase=%" PRId32 " capture=%" PRId64
               " seed=%04x submitted=%d grid=%d same_grid=%d\n",
               e->seq, e->kind == BOARD_TOUCH_TRACE_RAW ? "raw" : "observer", e->at_us,
               e->read_seq, e->raw_seq, e->irq_seq, e->irq_at_us, e->x, e->y,
               e->pressed, e->edge, e->raw_at_us, e->raw_match, e->phase, e->capture_at_us,
               (unsigned)e->pending_seed, e->submitted, e->grid, e->same_grid);
        ++printed;
    }
    printf("echoy-touch rows=%u omitted=%u; IRQ windows are ambiguous, not lost-note counts; "
           "grid uses submitted callback time, not a controller timestamp\n", printed, significant - printed);
    heap_caps_free(events);
}
#endif
