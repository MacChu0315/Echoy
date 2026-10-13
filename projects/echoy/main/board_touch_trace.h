#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { BOARD_TOUCH_TRACE_CAPACITY = 512, BOARD_TOUCH_IRQ_CAPACITY = 64 };
typedef enum {
    BOARD_TOUCH_TRACE_INACTIVE,
    BOARD_TOUCH_TRACE_ARMED,
    BOARD_TOUCH_TRACE_CAPTURING,
    BOARD_TOUCH_TRACE_FROZEN
} board_touch_trace_state_t;
typedef enum {
    BOARD_TOUCH_TRACE_READ,
    BOARD_TOUCH_TRACE_RAW,
    BOARD_TOUCH_TRACE_OBSERVER
} board_touch_trace_kind_t;

/* RAW is the driver's returned state before public coordinate transforms.
 * edge: -1 release, 0 unchanged, 1 press, 2 initial sample (not a proven edge).
 * IRQ sequence values identify observation windows, not physical touch IDs. */
typedef struct {
    uint32_t seq, read_seq, raw_seq, irq_seq, irq_end_seq;
    int64_t at_us, end_us, irq_at_us, raw_at_us, capture_at_us;
    int32_t result, x, y, phase;
    uint16_t pending_seed;
    int8_t edge, grid;
    uint8_t kind, points;
    bool pressed, submitted, raw_match, same_grid;
} board_touch_trace_event_t;

typedef struct {
    uint32_t seq;
    int64_t at_us;
} board_touch_irq_event_t;

typedef struct {
    uint32_t irq_count, irq_overwritten;
    uint32_t reads, read_errors, raw_reports, raw_presses, raw_releases;
    uint32_t observers, observer_presses, submitted, same_grid;
    uint32_t multiple_irq_windows, irq_during_read;
    uint32_t event_count, event_overwritten, busy_drops;
    uint32_t events_copied, events_omitted, irqs_copied, irqs_omitted;
    int64_t max_read_gap_us, max_read_duration_us, max_latest_irq_age_us;
    int64_t max_raw_to_observer_us;
    uint32_t state;
} board_touch_trace_summary_t;

#ifdef ESP_PLATFORM
#include "esp_lcd_touch.h"

/* One physical handle and one GSP reader. Attach before GSP starts. Wrappers
 * forward each original callback exactly once; no new reads or ACKs. Never
 * changes interrupt_callback, user_data or process_coordinates. */
esp_err_t board_touch_trace_attach(esp_lcd_touch_handle_t touch);

/* Only after GSP input and its interrupt adapter have stopped. The caller
 * owns lifecycle serialization; this API cannot infer framework task state.
 * Refuses to replace callbacks another owner changed after attachment. */
esp_err_t board_touch_trace_detach(void);
void board_touch_trace_wake_from_isr(void *unused);

/* Task context only. Reset at successful Record action. Snapshots are copied
 * consistently or return false without waiting. Writers never wait on a
 * diagnostic reader: skipped records are counted in busy_drops. Arrays get
 * the latest retained entries in sequence order; omission counts are explicit.
 * Do not call lifecycle or dump APIs on the audio owner. */
void board_touch_trace_reset(void);
/* Feed the cached phase on each UI refresh. ARMED tolerates stale IDLE cache;
 * after seeing COUNT_IN/RECORDING, leaving capture freezes the observations.
 * Explicit freeze after accepted Cancel also covers cancel before that cache
 * transition. Frozen snapshots remain readable until the next reset. */
void board_touch_trace_note_phase(int phase);
void board_touch_trace_freeze(void);
void board_touch_trace_note_observer(int x, int y, bool pressed, int64_t callback_at_us,
                                    int phase, int64_t capture_at_us, uint16_t pending_seed,
                                    bool submitted);
bool board_touch_trace_snapshot(board_touch_trace_summary_t *summary,
                                board_touch_trace_event_t *events, size_t event_capacity,
                                board_touch_irq_event_t *irqs, size_t irq_capacity);
/* Explicit diagnostic/UI task only, after capture. Bounded summary + at most
 * 64 edge/observer rows. No printf/logging occurs on the input hot paths. */
void board_touch_trace_dump(void);
#else
/* Shared native UI needs no ESP dependencies or additional linked source. */
static inline void board_touch_trace_reset(void) {}
static inline void board_touch_trace_note_phase(int phase) { (void)phase; }
static inline void board_touch_trace_freeze(void) {}
static inline void board_touch_trace_note_observer(int x, int y, bool pressed,
                                                 int64_t callback_at_us, int phase,
                                                 int64_t capture_at_us, uint16_t pending_seed,
                                                 bool submitted)
{
    (void)x;
    (void)y;
    (void)pressed;
    (void)callback_at_us;
    (void)phase;
    (void)capture_at_us;
    (void)pending_seed;
    (void)submitted;
}
static inline void board_touch_trace_dump(void) {}
#endif
