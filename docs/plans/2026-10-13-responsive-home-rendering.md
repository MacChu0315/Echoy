# Responsive home rendering

Status: explicitly approved and implemented on 2026-10-13. Package `09275b935ad8`
is installed on both authorized boards. Counted left recording, perceived
response improvement, pause and Harmony home passed the targeted physical
check; on 2026-10-14 the user also confirmed normal same-version cooperation.
Evidence:
`../verification/background-home/README.md`.

## Problem and evidence

The user clarified that the complaint is missing or late feedback immediately
after tapping, rather than missing notes in the final six-tap audition. On
package `73058c98b7ce`, six explicitly counted taps produced six observer press
events and six stored source notes, without same-grid merges or diagnostic
loss. Controller reads took at most 742 us and raw-to-observer delivery at most
174 us, but reads were as much as 149755 us apart. Another uncounted rhythm had
a 226183 us maximum read interval. These intervals are not direct measurements
of physical finger-to-sound latency.

The pinned GSP runtime polls touch, feeds its input state and executes a render
iteration on the same task. Its touch IRQ only records a sequence and wakes
that task; it does not sample or queue a full controller report. The existing
home Canvas callback performs the complete CPU rasterization on this task.

A small completed optimization skips Voice segments outside each raster block.
It reduces one frame's segment checks from 100800 to 7640 and passes 3712-frame
pixel equivalence. Paired PC Voice rendering improves about 15% versus the
current device source. This can shorten a stall; it does not remove input's
dependence on completing each rasterization. The new package is locally built
and its native GSP UI checks pass, but it has not been installed.

## Proposed implementation

Move only the expensive home-art raster calculation to a bounded, lower-priority
producer. Keep GSP as the sole physical touch reader and retain Echoy's current
pointer callback, command queue, recording timestamps, cancellation, sound owner
and continuous-play permission. Do not introduce a second controller reader,
extra ACKs, debug input injection or a separate musical input path.

The UI owner submits a copied immutable frame/plan through a nonblocking
latest-request mailbox. A busy mailbox leaves the request pending for a later
UI tick; no input callback waits for the worker. The worker uses its own art
workspace and two RGB565 output buffers. It calls only the existing pure C
rasterizer, with no GSP, flash, filesystem, sensor, audio or network I/O.

Completed buffers return to the UI owner through a bounded ready slot. The UI
checks the structural generation and submits a valid frame with the public
`esp_gsp_canvas_try_push()`. Queue-full keeps ownership locally and retries on
a later tick; it never calls blocking push. GSP's release callback returns an
accepted buffer exactly once. The worker never writes a borrowed buffer.
The UI owner remains responsible for all GSP calls and scene decisions.

Structural generation covers role/expression, committed or capture-plan change,
recording generation/phase, route and recovery/removal. It prevents a late
completed frame from restoring old recording or role content. Normal progress
and energy updates coalesce; they must not invalidate every completed animated
frame and starve publication. A paused/static final frame remains pending until
accepted. Existing small feedback-row drawing stays on GSP, so its recorded
markers and the other controls can respond without the full art calculation.

Preserve the exact 480x480 layout, existing glyphs, curves, antialiasing, bottom
button bounds, countdown, four recording beats, one-shot and musical behavior.
The expensive central animation updates when its next computed frame is ready;
the goal is to free touch/control processing from its CPU cost, not to promise
30 new central-art frames per second on this board.

## Memory and lifetime

Two 300x204 RGB565 buffers require 244800 bytes. The separate raster workspace,
copied plans and pending metadata keep total additional PSRAM below about
0.5 MiB. Explicit PSRAM allocation must not fall back to internal RAM. The
current build already enables `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`;
use the public `xTaskCreateWithCaps`/matching deletion API for a measured,
PSRAM-backed worker stack, since this worker performs no cache-disabling I/O.
Keep the small task/control overhead bounded and measure actual internal heap
and stack margins; previous internal-memory failures remain relevant.

Initialization must handle every allocation/task failure without starting a
partially owned producer. Use the existing synchronous renderer as a clearly
logged startup fallback if producer creation fails. Runtime rendering/submission
errors remain explicit; do not hide them with unchecked retries.

Deletion first prevents new UI requests, then stops the producer, stops the
Canvas stream, and waits through the existing public flush/release barriers
before freeing frames, context or workspace. Timeout or failure retains live
storage for a retry, as current Echoy UI teardown already requires. A release
callback must not wait, allocate or access a freed producer. The audio owner
never participates in any worker/GSP wait.

Native simulator support must exercise the same mailbox, generation and buffer
ownership logic with a host worker, not silently retain only the old rendering
path during validation. Gallery encoding remains unchanged in this scope.

## Verification before installation

1. Deterministic host tests hold rasterization for 200 ms while UI control/touch
   processing continues; use immutable input copies, not wall-clock assertions.
2. Exercise coalesced requests, zero-free-buffer/queue-full cases, all immediate
   errors, exactly-once release, role/record/cancel changes during a job, final
   paused frames and no starvation under ongoing animation.
3. Test partial initialization, active-worker shutdown, failed flush/release and
   retry without use-after-free, deadlocks or additional audio operations.
4. Compare completed frames with the existing rasterizer, then run actual native
   GSP recording/cancel/pause/role flows with the producer enabled. Keep the
   frozen HTML unchanged and run applicable sanitizers.
5. Build and inspect the pinned-SDK package. Install first on one authorized,
   separated device; validate startup, retained layout, internal heap and worker
   stack. Repeat counted recording and compare read intervals and subjective
   immediate response. A preserved six-tap sequence alone is insufficient.
6. Recheck playback/role transitions and explicit pause, then update the second
   authorized board and verify cooperation. Harmony audio parameters remain
   unchanged until its separate exact-plan/acoustic investigation.

The user explicitly approved this change to drawing/task and buffer ownership.
Existing firmware installation authorization remains valid and does not need
to be requested again.
