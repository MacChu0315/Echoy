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
The owner drains bounded batches every 10 ms, samples contact every 20 ms,
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

`CONFIG_ECHOY_DEFAULT_ENGLISH` is a local build-time default only before saved
settings exist. The normal product still defaults to Chinese and Pulse.
Demo aliases belong in a computer-side UID mapping; firmware does not derive
roles from MAC suffixes or replace real UIDs.

Screen begin allocates one full 460800-byte RGB565 surface in aligned PSRAM,
then uses `lv_snapshot_take_to_draw_buf()` on the complete 480x480 active LVGL
screen under the display lock. No GSP types or partial flush buffers are used.
Read returns bounded chunks from that immutable frame. After the final chunk,
a new offset-zero read captures a new complete frame for pull streaming. End
is idempotent and frees the surface; the inspected Iris service invokes end
on completion, stop and disconnect. Capture does not take an audio lock. Its
CPU/PSRAM impact and exact display appearance remain hardware tests.

## Required physical checks (not yet performed)

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

Host CTest and sanitizer results are in the Task 8 report. None of these
physical checks is claimed by a firmware build or a host simulation.
