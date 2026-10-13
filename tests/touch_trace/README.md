# Application touch trace

Run `sh tests/touch_trace/run.sh`. The fake physical controller uses the pinned
public `esp_lcd_touch.h`, rather than a replacement touch-handle layout. It checks
exactly-once calls, unchanged arguments/outputs/errors, independent IRQ ownership,
attachment failure, restoration/refusal to detach during an active read or after
another callback replacement, ring wrap/truncation/reset, nearest 156250 us grid
collisions and concurrent snapshots across 20,000 real wrapper calls. A separate
native compile excludes all ESP headers. Explicit dump output is capped at 67 lines.

Use `ECHOY_TOUCH_TRACE_SANITIZE=1 sh tests/touch_trace/run.sh` for ASan/UBSan/LSan.
If a ptrace-based runner prevents LeakSanitizer from inspecting threads, run
the same local command outside that restriction. The complete sanitizer suite
passed there. The tests also assert diagnostic allocation/free balance.

## Integration

Add `board_touch_trace.c` to firmware sources. After `board_touch_init(&touch)`,
before starting GSP, call `board_touch_trace_attach(touch)`. On success set:

```c
lcd.touch_wake_from_isr = board_touch_trace_wake_from_isr;
lcd.touch_wake_user_ctx = NULL;
```

If attachment fails, continue with the unmodified touch input and report that
diagnostics are unavailable. Do not register a second GPIO interrupt callback or
alter the driver's `config.user_data`. Only call `board_touch_trace_detach()`
after GSP input and its interrupt adapter have stopped; a UI deletion alone does
not stop the LCD render task. The trace restores both original driver callbacks.

Attachment starts inactive (state 0). In the UI call
`board_touch_trace_reset()` after a successful Record action to arm (state 1).
Pass each cached phase to `board_touch_trace_note_phase()`: seeing count-in or
recording enters capture (state 2); leaving those phases freezes the observations
(state 3). An accepted Cancel calls `board_touch_trace_freeze()` explicitly,
including cancellation before the UI has seen the asynchronous capture state.
Reset/freeze requests survive diagnostic snapshot contention. Frozen rings
remain readable and are not overwritten by later playback or idle input.
Call `board_touch_trace_note_observer(x, y, pressed, callback_at_us, phase,
capture_at_us, pending_seed, submitted)` once per pointer observer, passing its
original callback timestamp and the result of the existing TAP action. A
successful submission is not proof that the asynchronous model committed it.
Production UI never calls the logging dump: read the frozen capture explicitly
through ECA1 method 3 and `tools/capture_touch_trace.py`, as documented in
`../snapshot_rpc/README.md`. The optional dump helper is limited to explicit
diagnostics outside recording and never runs on the PCM owner.
Native UI calls compile to inline no-ops and require no native source-list change.

## Interpretation and limits

- The 64-entry IRQ ring and IRQ snapshot scratch live in static INTERNAL RAM.
  The ISR records only sequence/time. The 512-entry task ring is PSRAM-only;
  no task, controller read, ACK, allocation or printing is added by the ISR.
  A dump temporarily allocates another bounded 512-entry PSRAM buffer.
- Raw coordinates precede `process_coordinates` and public mirror/swap handling;
  observer coordinates follow those transforms. Initial raw state has edge 2;
  it is not evidence of a press transition. Only actual returned transitions
  contribute to raw press/release totals.
- Read callbacks keep their original return codes, including failure. Every
  normal driver call is forwarded once. No additional controller reads occur.
- `multiple_irq_windows` means more than one interrupt occurred between sampled
  read-start boundaries. `irq_during_read` is also explicit. Neither proves a
  missed physical touch: reports, ACKs and interrupt edges are not one-to-one.
- `max_latest_irq_age_us` measures age of the latest observed interrupt, even
  on later polls. It is not a physical touch latency measurement. Observer delay
  measures from the latest driver's get return, with equal pressed state; this
  match does not establish a physical touch identity.
- Grid uses the submitted callback time, rounds to the nearest 156250 us step,
  clamps the final in-window step to 15 and excludes times outside the recording
  window. `same_grid` counts repeated submitted grid positions for one capture,
  which intentionally collapse to one mask bit in the musical model.
- Event/IRQ overwrite and snapshot omission counts are separate. Task writers
  never wait for a snapshot; diagnostic contention skips trace records and
  increments `busy_drops`. If nonzero, read/raw/observer totals describe retained
  diagnostic observations and must not be treated as complete physical counts.
  Snapshot arrays and corresponding counters are consistent; interrupts can
  continue immediately after their snapshot boundary.

Host checks validate instrumentation behavior only. Physical touch drop rates,
render stalls and musical acceptance require measurements on the actual board.
