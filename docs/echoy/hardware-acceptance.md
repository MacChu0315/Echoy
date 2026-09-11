# Echoy device services and hardware acceptance

## Application settings

Only the default NVS partition's namespace `echoy`, key `settings`, is written.
The 64-byte schema-1 blob contains version, committed role/phrase, locale,
volume, four role activity memories, four phrase memories and a CRC32. Numbers
are little endian; normalized values use millionths. Reserved bytes are zero.
Schema/size/checksum/ranges/finite values are validated before use and before
writes. Failed reads select Pulse, Chinese, paused defaults and retain a
settings diagnostic. A missing key is an ordinary first boot. Failed writes
retain the pending value and an error; retries remain spaced by 500 ms.
Settings are staged only from committed role/phrase/activity. Volume and
locale also persist after 500 ms of quiet. There is no stored playing state,
session, pending transaction, boot ID or wireless identity. Reboot is paused.
`sysmeta` is independently initialized by the retained Recovery support and is
never erased by Echoy; no flash erase fallback is present.

## Ownership and resources

`echoy_control` is the only task that changes core/protocol/storage state. UI
and development RPC callbacks enqueue copied commands into a 16-entry queue
with zero wait. Radio callbacks use the existing 32-entry copied-event queue.
The owner drains bounded batches every 10 ms and samples the processing clock
again for each dequeued radio packet, carrying the newest time into its later
tick. Callback receive timestamps cannot become newer than a stale loop-start
time and cause otherwise valid packets to be rejected. It samples contact every 20 ms,
reads battery every 5 s and publishes diagnostics every 250 ms. All overflow
paths return failure/count drops; no callback blocks on codec, NVS or core.
Large runtime and forecast structures are allocated in PSRAM once. Queues,
mutexes and the 8192-byte owner stack are allocated and checked at startup.
Failure before task creation frees all allocations. Once the owner starts,
registered callback context lives for the application lifetime. Partial RPC
registration failures retain that live owner and Recovery rather than freeing
a context another task might use. Audio owns its DSP/codec and four-message
plan queue; audio failure maps to `ECHOY_OUTPUT_FAULT`, never `ECHOY_DATA_FAULT`.
Retry calls the checked speaker retry/start API on the owner.

UI snapshots are copied under a nonblocking BSP display lock at approximately
30 Hz. Ordinary and COMMIT-seen future bar plans are computed in a separate
core copy with 40 ms look-ahead. The real core still changes at its agreed
boundary. An independent monotonically increasing audio clock version cancels
unrendered notes when a forecast is aborted or its clock changes, including
returning to an earlier core clock version. This leaves the BSP's existing
6 x 240-frame DMA and its already-written samples unchanged. The actual
presentation lead, boundary skew and capacity margin need physical measurement.

## Real BSP services

Battery charge is `bsp_battery_read().state_of_charge`, never voltage or a
formatted string. A failed read or out-of-range sample preserves the previous
valid percent and increments diagnostics; it cannot request shutdown. Valid
values below 15 show the existing charge warning. A real valid 0 first powers
off the core and flushes pending application settings, then requests
`bsp_power_set_shutdown(true)`. The inspected BSP initializes power control and
asserts GPIO57 low as open drain. Failures are reported; no guessed rail or
sleep sequence is used. Development-injected 0 can test product state but
cannot actuate the real shutdown pin.

`bsp_imu_get_handle()` and `mosaico_module_mgr_get_info()` inspect existing
driver/manager state only. The latter copies its cached slot information under
its mutex. If no manager has started, diagnostics report unavailable. Echoy
does not start that manager, scan EEPROM, claim any slot, start IMU streaming,
or initialize a camera for this feature. Initial real cached module types and
states are included; development diagnostics can inject simulated states.
These diagnostics do not affect expression or music.

## Actual ESP-Iris interfaces

The initialized utils checkout supplies `esp_iris_rpc_register()` and
`esp_iris_screen_register()` in its public `esp_iris.h`. Product service
`0xec01`, method `1`, accepts an empty request and returns bounded UTF-8 JSON
(up to 4096 bytes). It includes real radio UID/boot, the Iris device ID/boot
mapping, session tuple/membership count, active/pending edges and revisions,
clock offset/rate/uncertainty/loss, synchronization status, output errors,
audio queue failures/pending notes/missed notes/voice peak/clipping, heap and
owner task watermarks, input/radio/publication drops, protocol rejections,
contact errors and battery/settings/IMU/module errors. Identity fields are
hex strings to avoid JavaScript integer precision loss. No pairing-token API
is called and no token is accepted by the diagnostic formatter.

`CONFIG_ECHOY_DEV_INJECTION` defaults off. Only development builds register:

- Method 2: three bytes, UI action enum plus little-endian uint16 value. Only
  play/recover/cancel/finish, role 0..3, phrase 1..3, and activity/volume 0..1000
  are accepted. Values are validated before the bounded queue.
- Method 3: one byte, locale 0 (Chinese) or 1 (English).
- Method 4: four bytes: diagnostic type 0 battery / 1 IMU / 2 module, value
  0..100, error code, slot selector. These are local test observations.
- Method 5: five bytes: pointer phase 0 down / 1 move / 2 up / 3 cancel,
  little-endian uint16 x and y, both 0..479. Validation precedes the bounded
  command queue. The owner takes the display lock without waiting and calls
  the same `echoy_view_pointer()` as the product renderer. Unavailable display
  or a busy lock increments `touch_dropped` and retains a pending gesture
  cancellation. Every control-task iteration retries that cancellation even
  without new input. Once the lock is available it calls `UI_CANCEL` exactly
  once; a newly accepted pointer event follows that cancellation under the
  same lock. Repeated busy locks cannot clear the pending flag. This prevents
  a dropped UP/CANCEL from trapping the old capture and swallowing the next
  Play gesture. `touch_processed` excludes synthesized cancellations;
  `touch_cleanups` counts them, and `touch_cancel_pending` exposes pending work.
  Accepted RPC queueing alone is not proof that a touch was processed. This
  bypasses the physical sensor and cannot measure its latency or calibration.

The pinned BSP registers physical input through
`esp_lv_adapter_register_touch()` in `onboard/display.c`. It goes directly to
the LVGL input path, not through development RPC method 5 or its command queue.
The shared view maps `LV_EVENT_RELEASED` to `UI_UP` and `LV_EVENT_PRESS_LOST` to
`UI_CANCEL`. This change retains cancellation for the owner-delivered RPC path;
it does not replace the BSP input driver or claim physical release-loss testing.
The delivery state and counters have one control-task owner; RPC callbacks
only enqueue copied bytes. View cancellation and input both use the same BSP
display lock that serializes access against LVGL/native input processing.

Final acceptance instrumentation additionally exports DSP `started`/`active`,
PSRAM free bytes, owner UI snapshot updates, development touch processed/drop
counts, rejected audio plans and maximum render/write duration. `ui_updates`
counts successful owner publications, not LCD refreshes. `audio.gaps` counts
software presentation catch-ups, not measured hardware DMA underruns.

`CONFIG_ECHOY_DEFAULT_ENGLISH` is a local build-time default only before saved
settings exist. The normal product still defaults to Chinese and Pulse.
Demo aliases belong in a computer-side UID mapping; firmware does not derive
roles from MAC suffixes or replace real UIDs.

Screen begin allocates one full 460800-byte RGB565 surface in aligned PSRAM,
then uses `lv_snapshot_take_to_draw_buf()` on the complete 480x480 active LVGL
screen under the display lock. No GSP types or partial flush buffers are used.
Read returns bounded chunks from that immutable frame. Random-access reads retain one immutable frame, including tail-first and repeated
chunks. The backend tracks a contiguous traversal starting at offset zero. Only
when that traversal has covered all 460800 bytes may the next offset-zero read
capture a fresh frame. A gap, repeat, post-completion nonzero read or invalid
request resets traversal tracking without replacing the frame. This matches
Iris `prepare_pull_screen()`, which increments its offset by each read size and
resets offset to zero at frame completion without calling end/begin again.

A refresh captures into a temporary second frame before replacing the old one;
the retained allocation is one frame and the refresh peak is two frames. Failed
capture or a changed size releases the temporary allocation, preserves the old
frame and completed traversal, and allows another offset-zero retry. End is
idempotent, frees retained storage and resets traversal tracking; the inspected
Iris service invokes it on completion, stop and disconnect. Capture does not take an audio lock. Its
CPU/PSRAM impact and exact display appearance remain hardware tests.

## Recorded Task 10 attempt — 2026-09-12

The local host environment and tooling were exercised. An initial sandbox
`mosaico.py list` could not create its pinned host environment in the user's
state directory. The approved host-permission invocation prepared that
environment and started the tool-managed local Gateway. A successful Iris
list returned no devices. A sandbox `/dev/ttyACM0` check was initially absent;
that was **not** evidence of physical absence. The host-permission check found
the authorized `/dev/ttyACM0`, a unique registered ESP32-S31 ROM interface.

`mosaico.py recover --dry-run` with the checked explicit `IDF_PATH` verified
the reviewed **2.8.5-recovery** bundle and one ROM candidate. On the authorized
board, the subsequent `mosaico.py recover` wrote and hash-verified the reviewed
images and performed its reset. It reached Gateway identity validation after
104.8 seconds, but the final request failed with:

```text
HTTP 409: Recovery endpoint reattached but identity verification did not complete
```

The command exited **4** (`device_unavailable`). A later host-permission check
showed that `/dev/ttyACM0` and its ESP32-S31 by-id entry were no longer present;
the two unrelated USB/UART adapters were not selected or opened. Subsequent
Iris lists remained empty. No real Iris Device ID/Boot ID, Recovery ready
response, application health or on-device Echoy diagnostic was obtained.
**Recovery transmission passed; Recovery startup/identity did not pass.**

No `install` was attempted against an unverified endpoint. No Echoy normal or
development application was installed during this attempt, so no actual
ES8311 playback, hardware screenshot, physical touch, board endurance,
normal→Recovery→normal cycle or speaker recording is claimed. The board needs
onsite power/USB/reset inspection; once it returns, use a fresh `mosaico.py
list` and complete retained-Recovery verification before any further write.
Do not repeat the flash blindly or switch to the unrelated serial adapters.

Local evidence:

- `output/task10/device-list-initial.json`,
  `device-list-before-recover.json`, `recover-preflight.json`;
- `output/task10/host-device-nodes.log`,
  `host-device-after-recover.log`, `device-list-after-recover.json`;
- `output/task10/device-recover.stderr` (the CLI emits its error JSON on
  stderr; `device-recover.json` is consequently empty);
- `.codex-runs/mosaico/20260911T182730Z-recover/raw.log`, including the reviewed
  bundle validation, write/hash/reset and failed maintenance verification.

Normal and temporary development builds were compiled successfully. Method 5
and instrumentation have host validation and IDF compile evidence, **not
hardware execution evidence**. The project build is restored to the normal
default-off injection configuration for handoff; the development binary is a
local ignored artifact only. The exact build evidence, complete requirement
coverage and remaining physical rows are linked in
[requirements-matrix.md](requirements-matrix.md).

## Completed PC endurance — not hardware acceptance

`python3 tools/echoy-sim/endurance.py --seconds 1800 --output
output/task10/host-endurance` exited 0 after **1800.006 seconds**. Three real
C simulator processes produced **1,554** three-frame publications, processed
**4,662** LVGL pointer events and supplied **4,185** nonzero PCM publications.
All three exited normally, remained playing during every observation and ended
with zero runner-rejected inputs. At most four radio packets remained queued
at a publication for the next delivery pass (capacity 4,096); no queue overflow
occurred.

Initial process RSS was 6,196 / 6,048 / 6,216 KiB. Final and peak RSS was
6,196 / 6,108 / 6,276 KiB: B/C gained 60 KiB at startup, then stayed stable.
The three-frame publication time was P50 **33.53 ms**, P95 **46.75 ms**, maximum
**126.12 ms**. These are PC frame-generation/read times, not physical input
latency, LCD refresh rate or acoustic onset. PCM came from the shared C DSP,
not a browser synthesizer. The independently paced simulated clock ended at
1,800.213 seconds, including its explicit startup preparation.

Raw samples and summary: `output/task10/host-endurance/samples.jsonl` and
`summary.json`. MCU heap/stack/CPU, hardware underruns, touch sensor response,
Iris contention, real wireless packets and speaker/acoustic output were not
measured by this test.

## Final integration regression evidence

`output/final-fix/` records 40/40 CTest and 40/40 ASan/UBSan passes,
13/13 JS adapter checks, 29 headless scenarios and 23 real Chrome checks.
Wire-owner tests cover both source directions for fault/loading/recovery,
apply-boundary revision/motif/source consistency, last-peer clock continuity
and local commits, and visible COMMIT-loss failure followed by recovery.
Shared LVGL and live C runner tests now verify Pulse onset/decay and Voice notes,
including clock replacement and silent-output cases. A subsequent scoped fix
also retains the exact core `audible_since_us` in each plan, preventing waiting
or fault/mute recovery from replaying notes from a skipped silent window.
Round-2 regression/build evidence is in `output/final-fix/round2/`. These do not measure actual
LCD-versus-codec presentation delay.

The browser runs its actual `App.start()` with 60/126 ms responses for 1200 ms
in regression tests. It retains elapsed time during busy responses, catches up
with at most one 250 ms request, preserves that PCM interval, and uses a 192 ms
worklet prefill with 750 ms maximum buffering. Disconnect/reconnect discards
offline time. This improves the interactive PC preview and adds bounded preview
latency; it cannot validate a hardware acoustic timing target.

The first integration wave normal / development / restored-normal IDF builds
passed at 3,847,152 / 3,847,792 /
3,847,152 bytes. `firmware-final.json` records the final ordinary sdkconfig,
injection disabled, absent `inject_rpc`, retained Recovery and ota_0 capacity.
The known app/factory warning remains; reconfiguration additionally reports the
five existing upstream private-include CMake warnings. No board operation was
retried. The final audible-window normal build is 3,847,792 bytes, with injection
off and no `inject_rpc`; see `round2/firmware-final.json`. All physical checkboxes
and the existing Recovery blocker remain open.

## Required physical checks (still pending)

- Restart after role/phrase/activity/volume/locale changes; verify paused
  restoration and retained Recovery/system identity across bad app settings.
- Read real charge below 15 and inject read errors; on a safely controlled
  battery setup validate GPIO57 shutdown and verify sysmeta retention.
- Run one speaker while repeatedly opening/closing and disconnecting Iris
  screenshot/stream requests. Measure PSRAM returning to baseline, task/heap
  watermarks, underruns and physical audible output continuity.
- Confirm ES8311 playback, real magnetic neighbor classification, 2+1 admission,
  nonleader changes, reference power-off/return and continuous acoustic phase.
- Measure actual 6 x 240 DMA latency, prepared COMMIT cancellation near the
  boundary, and 40 ms forecast margin under UI/screenshot/NVS contention.
- Validate product error pages and Iris Recovery when display/codec initialization
  fails. Healthy marking now waits for a ready owner, a real successful speaker
  status and successful UI startup; starting Iris alone does not mark healthy.

Current host CTest and sanitizer results are under `output/final-fix/`. None of these
physical checks is claimed by a firmware build or a host simulation.
