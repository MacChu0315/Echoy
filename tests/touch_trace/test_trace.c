#include "board_touch_trace.h"
#include "echoy_model.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static atomic_llong clock_us;
static bool fail_alloc, touched, detach_in_read, irq_in_read, reset_in_read;
static _Thread_local bool snapshot_worker;
static atomic_bool block_snapshot, snapshot_at_gate, release_snapshot;
static unsigned allocations, frees, reads, gets;
static bool release_deferred;
static pthread_t deferred_worker;
static esp_err_t read_result;
static esp_lcd_touch_handle_t expected_touch;
static void *expected_user, *expected_driver;
static uint16_t *expected_x, *expected_y, *expected_strength;
static uint8_t *expected_points, expected_capacity;
static board_touch_trace_event_t events[BOARD_TOUCH_TRACE_CAPACITY];
static board_touch_irq_event_t irqs[BOARD_TOUCH_IRQ_CAPACITY];

int64_t esp_timer_get_time(void) { return atomic_load(&clock_us); }
void touch_test_critical_enter(portMUX_TYPE *lock)
{
    if (snapshot_worker && atomic_load(&block_snapshot)) {
        atomic_store(&snapshot_at_gate, true);
        while (!atomic_load(&release_snapshot)) sched_yield();
    }
    assert(!pthread_mutex_lock(lock));
}
void *heap_caps_calloc(size_t n, size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fail_alloc) return NULL;
    ++allocations;
    return calloc(n, size);
}
void heap_caps_free(void *p) { if (p) ++frees; free(p); }
static void interrupt(esp_lcd_touch_handle_t tp) { (void)tp; }
static void transform(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *s,
                      uint8_t *n, uint8_t max)
{ (void)tp; (void)x; (void)y; (void)s; (void)n; (void)max; }
static void finish_deferred_reset(void)
{
    if (!release_deferred) return;
    atomic_store(&release_snapshot, true);
    assert(!pthread_join(deferred_worker, NULL));
    atomic_store(&block_snapshot, false);
}
static esp_err_t read_driver(esp_lcd_touch_handle_t tp)
{
    assert(tp == expected_touch && tp->config.user_data == expected_user &&
           tp->config.driver_data == expected_driver);
    ++reads;
    finish_deferred_reset();
    if (detach_in_read) assert(board_touch_trace_detach() == ESP_ERR_INVALID_STATE);
    if (irq_in_read) board_touch_trace_wake_from_isr(NULL);
    if (reset_in_read) board_touch_trace_reset();
    atomic_fetch_add(&clock_us, 35);
    return read_result;
}
static bool get_driver(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *s,
                       uint8_t *n, uint8_t max)
{
    assert(tp == expected_touch && x == expected_x && y == expected_y &&
           s == expected_strength && n == expected_points && max == expected_capacity);
    ++gets;
    finish_deferred_reset();
    if (n) *n = touched ? 2 : 0;
    if (touched && x && y && max >= 2) {
        x[0] = 42; y[0] = 84; x[1] = 142; y[1] = 184;
        if (s) { s[0] = 7; s[1] = 8; }
    }
    return touched;
}
static esp_lcd_touch_t make_touch(void)
{
    esp_lcd_touch_t t = { .read_data = read_driver, .get_xy = get_driver };
    t.config.interrupt_callback = interrupt;
    t.config.process_coordinates = transform;
    t.config.user_data = expected_user = (void *)0x1234;
    t.config.driver_data = expected_driver = (void *)0x5678;
    read_result = ESP_OK;
    touched = false;
    detach_in_read = irq_in_read = reset_in_read = false;
    reads = gets = 0;
    return t;
}
static bool get(esp_lcd_touch_t *t, uint16_t *x, uint16_t *y, uint16_t *s, uint8_t *n,
                 uint8_t max)
{
    expected_x = x; expected_y = y; expected_strength = s;
    expected_points = n; expected_capacity = max;
    return t->get_xy(t, x, y, s, n, max);
}
static board_touch_trace_summary_t snapshot(void)
{
    board_touch_trace_summary_t s;
    assert(board_touch_trace_snapshot(&s, events, BOARD_TOUCH_TRACE_CAPACITY,
                                      irqs, BOARD_TOUCH_IRQ_CAPACITY));
    return s;
}

/* Catches extra controller reads/ACKs, transformed arguments, suppressed driver
 * errors, false-result output changes, and GSP callback/user-data replacement. */
static void forwarding_and_lifecycle(void)
{
    esp_lcd_touch_t t = make_touch(), second = make_touch();
    expected_touch = &t;
    fail_alloc = true;
    assert(board_touch_trace_attach(&t) == ESP_ERR_NO_MEM);
    assert(t.read_data == read_driver && t.get_xy == get_driver);
    fail_alloc = false;
    assert(board_touch_trace_attach(NULL) == ESP_ERR_INVALID_ARG);
    assert(board_touch_trace_attach(&t) == ESP_OK);
    board_touch_trace_reset();
    assert(board_touch_trace_attach(&second) == ESP_ERR_INVALID_STATE);
    assert(t.config.interrupt_callback == interrupt && t.config.process_coordinates == transform);
    atomic_store(&clock_us, 1000);
    board_touch_trace_wake_from_isr(NULL);
    atomic_store(&clock_us, 1200);
    board_touch_trace_wake_from_isr(NULL);
    atomic_store(&clock_us, 1300);
    assert(t.read_data(&t) == ESP_OK && reads == 1);
    uint16_t x[2] = {9, 9}, y[2] = {8, 8}, strength[2] = {7, 7};
    uint8_t n = 99;
    assert(!get(&t, x, y, strength, &n, 2));
    assert(gets == 1 && n == 0 && x[0] == 9 && y[0] == 8 && strength[0] == 7);
    touched = true;
    assert(get(&t, x, y, strength, &n, 2));
    assert(gets == 2 && n == 2 && x[0] == 42 && x[1] == 142 && y[1] == 184 && strength[1] == 8);
    read_result = 0x7788;
    atomic_store(&clock_us, 2000);
    assert(t.read_data(&t) == 0x7788 && reads == 2);
    touched = false;
    assert(!get(&t, x, y, NULL, &n, 2));
    assert(!get(&t, NULL, NULL, NULL, NULL, 0));
    board_touch_trace_summary_t s = snapshot();
    assert(s.irq_count == 2 && s.reads == 2 && s.read_errors == 1 && s.raw_reports == 4);
    assert(s.raw_presses == 1 && s.raw_releases == 1 && s.multiple_irq_windows == 1);
    assert(s.max_read_duration_us == 35 && s.max_read_gap_us == 700);
    assert(s.max_latest_irq_age_us == 800);
    assert(events[0].irq_seq == 2 && events[0].at_us == 1300 && events[0].end_us == 1335);
    assert(events[3].result == 0x7788 && events[3].read_seq == 2);
    assert(irqs[0].at_us == 1000 && irqs[1].seq == 2);
    /* Refuse detach if another owner replaced either callback; do not partly
     * restore the other slot or free storage while its wrapper is installed. */
    bool (*wrapped_get)(esp_lcd_touch_handle_t, uint16_t *, uint16_t *, uint16_t *, uint8_t *, uint8_t) = t.get_xy;
    t.get_xy = get_driver;
    assert(board_touch_trace_detach() == ESP_ERR_INVALID_STATE);
    assert(t.read_data != read_driver);
    t.get_xy = wrapped_get;
    assert(board_touch_trace_detach() == ESP_OK);
    assert(t.read_data == read_driver && t.get_xy == get_driver);
    assert(t.config.interrupt_callback == interrupt && t.config.user_data == expected_user);
    board_touch_trace_wake_from_isr(NULL);
    assert(!board_touch_trace_snapshot(&s, events, 1, irqs, 1));
    assert(board_touch_trace_detach() == ESP_OK);
    assert(allocations == frees);
}

/* Catches ring indexing/truncation errors and wrongly turning IRQ multiplicity
 * into fabricated raw presses. Only controller-returned transitions count. */
static void wrap_reset_and_summary(void)
{
    esp_lcd_touch_t t = make_touch();
    expected_touch = &t;
    assert(board_touch_trace_attach(&t) == ESP_OK);
    board_touch_trace_reset();
    for (unsigned i = 1; i <= 70; ++i) {
        atomic_store(&clock_us, i * 10);
        board_touch_trace_wake_from_isr(NULL);
    }
    for (unsigned i = 0; i < BOARD_TOUCH_TRACE_CAPACITY + 5; ++i) {
        atomic_store(&clock_us, 10000 + i * 100);
        assert(t.read_data(&t) == ESP_OK);
    }
    board_touch_trace_summary_t s = snapshot();
    assert(s.irq_count == 70 && s.irq_overwritten == 6 && irqs[0].seq == 7 && irqs[63].seq == 70);
    assert(s.event_count == BOARD_TOUCH_TRACE_CAPACITY + 5 && s.event_overwritten == 5);
    assert(events[0].seq == 6 && events[BOARD_TOUCH_TRACE_CAPACITY - 1].seq == BOARD_TOUCH_TRACE_CAPACITY + 5);
    assert(s.raw_presses == 0 && s.raw_reports == 0 && s.multiple_irq_windows == 1);
    assert(board_touch_trace_snapshot(&s, events, 2, irqs, 3));
    assert(s.events_copied == 2 && s.events_omitted == BOARD_TOUCH_TRACE_CAPACITY - 2);
    assert(s.irqs_copied == 3 && s.irqs_omitted == 61);
    assert(events[0].seq == BOARD_TOUCH_TRACE_CAPACITY + 4 && irqs[0].seq == 68);
    board_touch_trace_reset();
    s = snapshot();
    assert(!s.irq_count && !s.reads && !s.event_count && !s.event_overwritten);
    assert(board_touch_trace_detach() == ESP_OK);
}

/* Catches incorrect floor rounding, missing grid collision accounting, and
 * falsely calling two callbacks one physical press or inferred missed notes. */
static void observer_timing_and_grid(void)
{
    esp_lcd_touch_t t = make_touch();
    expected_touch = &t;
    assert(board_touch_trace_attach(&t) == ESP_OK);
    board_touch_trace_reset();
    uint16_t x[2], y[2]; uint8_t n;
    atomic_store(&clock_us, 1001000);
    touched = true;
    assert(get(&t, x, y, NULL, &n, 2));
    board_touch_trace_note_observer(42, 84, true, 1002000, ECHOY_RECORDING, 1000000, 0, true);
    board_touch_trace_note_observer(42, 84, false, 1010000, ECHOY_RECORDING, 1000000, 1, false);
    board_touch_trace_note_observer(42, 84, true, 1070000, ECHOY_RECORDING, 1000000, 1, true);
    board_touch_trace_note_observer(42, 84, false, 1075000, ECHOY_RECORDING, 1000000, 1, false);
    board_touch_trace_note_observer(42, 84, true, 1078125, ECHOY_RECORDING, 1000000, 1, true);
    board_touch_trace_note_observer(42, 84, false, 3400000, ECHOY_RECORDING, 1000000, 3, false);
    board_touch_trace_note_observer(42, 84, true, 3499999, ECHOY_RECORDING, 1000000, 3, true);
    board_touch_trace_note_observer(42, 84, false, 3500000, ECHOY_IDLE, 1000000, 0, false);
    board_touch_trace_note_observer(42, 84, true, 3500001, ECHOY_IDLE, 1000000, 0, true);
    board_touch_trace_summary_t s = snapshot();
    assert(s.observers == 9 && s.observer_presses == 5 && s.submitted == 5 && s.same_grid == 1);
    assert(events[1].grid == 0 && events[1].raw_at_us == 1001000 && events[1].raw_match);
    assert(events[3].same_grid && events[3].grid == 0);
    assert(!events[2].raw_match && events[5].grid == 1);
    assert(events[7].grid == 15 && events[9].grid == -1);
    assert(board_touch_trace_detach() == ESP_OK);
}

/* Catches partial summary/event copies, out-of-order ring entries, silent
 * diagnostic contention loss, and callback teardown while a read is active. */
static atomic_bool reader_done;
static void *read_loop(void *ctx)
{
    esp_lcd_touch_t *touch = ctx;
    for (unsigned n = 0; n < 20000; ++n) assert(touch->read_data(touch) == ESP_OK);
    atomic_store(&reader_done, true);
    return NULL;
}
static void concurrent_snapshot_and_active_detach(void)
{
    esp_lcd_touch_t t = make_touch();
    expected_touch = &t;
    assert(board_touch_trace_attach(&t) == ESP_OK);
    board_touch_trace_reset();
    detach_in_read = irq_in_read = true;
    assert(t.read_data(&t) == ESP_OK);
    board_touch_trace_summary_t s = snapshot();
    assert(s.irq_count == 1 && s.irq_during_read == 1);
    assert(events[0].irq_seq == 0 && events[0].irq_end_seq == 1);
    detach_in_read = irq_in_read = false;
    board_touch_trace_reset();
    reads = 0;
    irq_in_read = true;
    pthread_t reader;
    atomic_store(&reader_done, false);
    assert(!pthread_create(&reader, NULL, read_loop, &t));
    unsigned snapshots = 0;
    do {
        if (!board_touch_trace_snapshot(&s, events, BOARD_TOUCH_TRACE_CAPACITY, irqs,
                                        BOARD_TOUCH_IRQ_CAPACITY)) continue;
        ++snapshots;
        assert(s.event_count == s.reads && s.raw_reports == 0);
        if (s.irqs_copied) {
            assert(irqs[s.irqs_copied - 1].seq == s.irq_count);
            for (uint32_t n = 1; n < s.irqs_copied; ++n) {
                assert(irqs[n].seq == irqs[n - 1].seq + 1);
                assert(irqs[n].at_us >= irqs[n - 1].at_us);
            }
        }
        if (s.events_copied) {
            assert(events[s.events_copied - 1].seq == s.event_count);
            for (uint32_t n = 0; n < s.events_copied; ++n) {
                assert(events[n].read_seq == events[n].seq);
                assert(events[n].result == ESP_OK && events[n].end_us - events[n].at_us == 35);
                if (n) assert(events[n].seq == events[n - 1].seq + 1);
            }
        }
    } while (!atomic_load(&reader_done));
    assert(!pthread_join(reader, NULL));
    s = snapshot();
    assert(snapshots > 0 && reads == 20000 && s.reads + s.busy_drops == reads && s.irq_count == 20000);
    assert(board_touch_trace_detach() == ESP_OK && allocations == frees);
}

/* Catches idle/late polls overwriting a finished recording, stale initial
 * cache freezing before capture, and early cancel leaving capture armed. */
static void recording_freeze_and_rearm(void)
{
    esp_lcd_touch_t t = make_touch();
    expected_touch = &t;
    assert(board_touch_trace_attach(&t) == ESP_OK);
    board_touch_trace_wake_from_isr(NULL);
    assert(t.read_data(&t) == ESP_OK);
    board_touch_trace_summary_t s = snapshot();
    assert(s.state == BOARD_TOUCH_TRACE_INACTIVE && s.irq_count == 0 && s.reads == 0);
    board_touch_trace_reset();
    board_touch_trace_note_phase(ECHOY_IDLE); /* Record command still queued. */
    s = snapshot();
    assert(s.state == BOARD_TOUCH_TRACE_ARMED);
    board_touch_trace_note_phase(ECHOY_COUNT_IN);
    board_touch_trace_note_phase(ECHOY_RECORDING);
    board_touch_trace_wake_from_isr(NULL);
    assert(t.read_data(&t) == ESP_OK);
    board_touch_trace_note_observer(40, 90, true, 1010000, ECHOY_RECORDING, 1000000, 0, true);
    s = snapshot();
    assert(s.state == BOARD_TOUCH_TRACE_CAPTURING && s.event_count == 2 && s.irq_count == 1);
    board_touch_trace_note_phase(ECHOY_PLAYING);
    board_touch_trace_summary_t frozen = snapshot();
    assert(frozen.state == BOARD_TOUCH_TRACE_FROZEN);
    unsigned original_reads = reads;
    uint16_t x[2], y[2]; uint8_t n;
    for (unsigned i = 0; i < 2 * BOARD_TOUCH_TRACE_CAPACITY; ++i) {
        board_touch_trace_wake_from_isr(NULL);
        assert(t.read_data(&t) == ESP_OK);
        assert(!get(&t, x, y, NULL, &n, 2));
        board_touch_trace_note_observer(40, 90, i % 2, 1010000 + i, ECHOY_PLAYING, 1000000, 1, false);
    }
    s = snapshot();
    assert(reads == original_reads + 2 * BOARD_TOUCH_TRACE_CAPACITY);
    assert(!memcmp(&s, &frozen, sizeof(s)) && events[1].kind == BOARD_TOUCH_TRACE_OBSERVER);
    board_touch_trace_reset();
    s = snapshot();
    assert(s.state == BOARD_TOUCH_TRACE_ARMED && !s.event_count && !s.irq_count);
    board_touch_trace_freeze(); /* Cancel accepted before cached COUNT_IN. */
    board_touch_trace_note_phase(ECHOY_IDLE);
    board_touch_trace_note_phase(ECHOY_COUNT_IN); /* Late stale cache cannot rearm. */
    s = snapshot();
    assert(s.state == BOARD_TOUCH_TRACE_FROZEN && !s.event_count && !s.irq_count);
    board_touch_trace_reset();
    board_touch_trace_note_phase(ECHOY_RECORDING);
    assert(t.read_data(&t) == ESP_OK);
    s = snapshot();
    assert(s.state == BOARD_TOUCH_TRACE_CAPTURING && s.reads == 1);
    assert(board_touch_trace_detach() == ESP_OK);
}

static void *gated_snapshot(void *unused)
{
    (void)unused;
    snapshot_worker = true;
    board_touch_trace_summary_t s;
    assert(board_touch_trace_snapshot(&s, NULL, 0, NULL, 0));
    return NULL;
}

/* The gate holds the task snapshot guard but no IRQ lock. One-shot controls
 * must return immediately, survive contention, and apply in request order. */
static void deferred_controls_and_read_generation(void)
{
    esp_lcd_touch_t t = make_touch();
    expected_touch = &t;
    assert(board_touch_trace_attach(&t) == ESP_OK);
    board_touch_trace_reset();
    assert(t.read_data(&t) == ESP_OK);
    atomic_store(&block_snapshot, true);
    atomic_store(&snapshot_at_gate, false);
    atomic_store(&release_snapshot, false);
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, gated_snapshot, NULL));
    while (!atomic_load(&snapshot_at_gate)) sched_yield();
    board_touch_trace_reset();
    board_touch_trace_note_phase(ECHOY_COUNT_IN);
    board_touch_trace_note_phase(ECHOY_IDLE);
    atomic_store(&release_snapshot, true);
    assert(!pthread_join(worker, NULL));
    atomic_store(&block_snapshot, false);
    board_touch_trace_summary_t s = snapshot();
    assert(s.state == BOARD_TOUCH_TRACE_FROZEN && !s.event_count && !s.irq_count);
    board_touch_trace_reset();
    board_touch_trace_wake_from_isr(NULL);
    board_touch_trace_wake_from_isr(NULL);
    reset_in_read = true; /* Force a session boundary after read's IRQ sample. */
    assert(t.read_data(&t) == ESP_OK);
    reset_in_read = false;
    s = snapshot();
    assert(s.state == BOARD_TOUCH_TRACE_ARMED && s.event_count == 0 && s.irq_count == 0);
    assert(s.busy_drops == 1 && s.multiple_irq_windows == 0 && s.irq_during_read == 0);
    assert(t.read_data(&t) == ESP_OK);
    s = snapshot();
    assert(s.reads == 1 && events[0].irq_seq == 0 && events[0].irq_end_seq == 0);
    assert(board_touch_trace_detach() == ESP_OK);
}

/* Regression from independent review: RESET has been consumed by a snapshot,
 * but it is stopped before clearing IRQs. A read starts in that window and
 * finishes after reset. No subtraction may mix its old/new IRQ sequences. */
static void reset_consumed_before_irq_reset(bool raw)
{
    esp_lcd_touch_t t = make_touch();
    expected_touch = &t;
    assert(board_touch_trace_attach(&t) == ESP_OK);
    board_touch_trace_reset();
    board_touch_trace_wake_from_isr(NULL);
    board_touch_trace_wake_from_isr(NULL);
    atomic_store(&block_snapshot, true);
    atomic_store(&snapshot_at_gate, false);
    atomic_store(&release_snapshot, false);
    pthread_t first;
    assert(!pthread_create(&first, NULL, gated_snapshot, NULL));
    while (!atomic_load(&snapshot_at_gate)) sched_yield();
    board_touch_trace_reset();
    atomic_store(&release_snapshot, true);
    assert(!pthread_join(first, NULL));
    atomic_store(&snapshot_at_gate, false);
    atomic_store(&release_snapshot, false);
    assert(!pthread_create(&deferred_worker, NULL, gated_snapshot, NULL));
    while (!atomic_load(&snapshot_at_gate)) sched_yield();
    release_deferred = true;
    unsigned before = raw ? gets : reads;
    if (raw) {
        uint16_t x[2], y[2]; uint8_t n;
        touched = true;
        assert(get(&t, x, y, NULL, &n, 2) && n == 2 && x[0] == 42 && y[0] == 84);
    } else {
        assert(t.read_data(&t) == ESP_OK);
    }
    release_deferred = false;
    board_touch_trace_summary_t s = snapshot();
    assert((raw ? gets : reads) == before + 1);
    assert(s.irq_during_read == 0 && s.multiple_irq_windows == 0);
    assert(s.reads == 0 && s.raw_reports == 0 && s.event_count == 0 && s.busy_drops == 1 && s.irq_count == 0);
    assert(t.read_data(&t) == ESP_OK);
    s = snapshot();
    assert(s.reads == 1 && events[0].irq_seq == 0 && events[0].irq_end_seq == 0);
    assert(board_touch_trace_detach() == ESP_OK);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--dump")) {
        esp_lcd_touch_t t = make_touch();
        expected_touch = &t;
        assert(board_touch_trace_attach(&t) == ESP_OK);
        board_touch_trace_reset();
        for (unsigned n = 0; n < 120; ++n)
            board_touch_trace_note_observer(42, 84, (n % 2) != 0, 1000000 + 20000 * n,
                                            ECHOY_RECORDING, 1000000, 0, (n % 2) != 0);
        board_touch_trace_dump();
        assert(board_touch_trace_detach() == ESP_OK && allocations == frees);
        return 0;
    }
    forwarding_and_lifecycle();
    wrap_reset_and_summary();
    observer_timing_and_grid();
    concurrent_snapshot_and_active_detach();
    recording_freeze_and_rearm();
    deferred_controls_and_read_generation();
    reset_consumed_before_irq_reset(false);
    reset_consumed_before_irq_reset(true);
    puts("PASS: touch forwarding/errors, active detach, coherent concurrent snapshots, ring wrap/reset and observer grid evidence");
    return 0;
}
