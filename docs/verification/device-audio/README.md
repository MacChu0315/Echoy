# Audio bring-up and bounded GSP startup

Target: `/dev/ttyACM0`, Mosaico v1.2 / ESP32-S31,
Device ID `4553502d49524953010030eda0f452a0`. All writes used specifically
authorized packages and official `mosaico.py iris system-update`. Recovery was
allowed to finish initialization before each submission.

## Verified hardware observations

- Binding package `afae9df52f28` installed successfully in operation
  `c2da2ecb-1098-49c5-a465-fa45cc83acb1`, boot `16880710469859591394`.
  [Identity/commit evidence](binding-fix-device-result.json).
- UI reached home and ES8311 opened at 24 kHz/16-bit mono. PCM timed out at
  4.362 seconds. The delayed acceptance task writes OTA/NVS 3 seconds after
  Iris starts at about 1.312 seconds; cached I2S interrupts cannot continue
  through every such flash operation. This is an unsafe startup overlap.
- The user tapped Restore, recorded/played a phrase and visited role/settings/
  guide. Streaming then continued with zero write/deadline failures. The user
  initially heard nothing, then confirmed **audible but very quiet sound**.
  The board's prior acoustic history is unknown. No waveform was measured.
- Observed: 1,222,080 accepted frames, render maximum 2,363 us, owner stack
  minimum 8,380 bytes, preparation worker minimum 8,192 bytes. One pulse phrase
  needed 10,354,001 us to prepare; this latency remains an optimization item.
- `ctl input` returned service-not-found. The touch evidence is physical user
  interaction, not a successful remote gesture script.

## Corrective implementation

Main now creates the UI with audio workers gated, calls public
`esp_iris_mark_healthy` to complete acceptance writes, then explicitly activates
audio. Failure or shutdown before activation never opens the speaker. Delayed
auto-accept is disabled; compile-time guards reject stale configuration. No
audio timeout was lengthened, error suppressed or automatic retry introduced.

Hardware volume rises from 40 to 60, as in existing BSP player examples: +10 dB
on the unchanged -50..0 dB curve. UI digital volume and synthesis are unchanged.
This is a configured gain change, not a measured acoustic level. Diagnostics
now retain accepted/nonzero PCM, peak, successful PA transitions and write
durations, including the statistics of a stream closed after failure.

Approved package `87197fd80c0d` was written in operation
`d2273dce-e365-4190-b3e5-da59b234d3bf`, but UI startup returned 263 and did not
report HEALTHY or activate audio. Read-only status/reconciliation preserved
[the operation](audio-startup-first-attempt.json) and
[the running identity](audio-startup-first-status.json); the uncertain operation
was not replayed.

The bounded-queue native fixture measures 27 initial component updates, plus
image/callback registration. Capacity 24 reproduces 263; capacity 64 passes.
Application defaults therefore select 64 slots, with a compile-time guard.
This changes queue capacity based on measured demand, not admission timeouts.

## Local verification

- `python3 tests/device_runtime/test_runtime.py`: 16/16 passed normally and with
  `ECHOY_RUNTIME_SANITIZE=1`; ASan/UBSan/default LeakSanitizer clean.
- `sh tests/board_audio/run.sh`: 22,263 assertions passed normally and with
  ASan/UBSan/default LeakSanitizer; partial-write diagnostics are included.
- The new activation test first failed on premature speaker opening, then passed
  with explicit activation and safe shutdown before activation.
- `ECHOY_TEST_QUEUE_DEPTH=24 ECHOY_TEST_LOADING=1 python3 tests/device_view/check.py
  --build-dir projects/echoy/build-sim-device-health --output <fresh-dir>`:
  expected failure 263. With depth 64 the same test passes startup, all five
  pages and unknown/37%/unknown readings. It models queue pressure, not real
  display scheduling.
- Official `idf.py ... system-update-bundle` succeeded in a fresh
  `output/device-audio-ui-build` directory. Only auto-accept policy and queue
  depth differ from the prior effective configuration. Full configure reports
  five existing SDK esp_wifi/wpa_supplicant private-include dependency warnings;
  there are no C/C++ compiler warnings. Vendor sources were not modified.

## Queue64 physical result

The user authorized the [final startup package](artifacts.json) `874d650babab`.
Operation `be8a0500-5d12-4db5-863c-6539e358a567` succeeded with HEALTHY,
boot `14556096418758680989`, and matching ELF hash
`7d136be5d292778cfc76c40493618716247f57f65b4bb74601a44cd128abc1fe`.
Bootloader/partition hashes are unchanged. The current Recovery boot's main-task
completion log was verified before the update; no installation replay occurred.

Official `iris device-status`, `iris logs --snapshot` and `iris screenshot`
verified the real home screen, capture → preparation → one-shot → idle and a
stable boot past 327 seconds. No 261/263 UI error or audio error occurred.
Latest observed streaming: 7,201,440 accepted frames, 2,677,015 nonzero frames,
peak 4,368, PA enabled, zero partial/write/deadline errors; render maximum 2,201 us,
write maximum 8,893 us, owner stack minimum 8,728 bytes. A pulse phrase took
9,553,429 us to prepare, still requiring optimization.
[Structured evidence](queue64-device-result.json).

## Volume calibration

The user reported that hardware volume 60 remained very quiet. PCM peak is about
13% full scale before another -20 dB hardware attenuation. The new
[volume-only package](volume90-artifacts.json) `d87ae02e9222` changes hardware
volume to 90 (-5 dB, +15 dB relative to 60) while retaining all digital controls,
musical logic, startup fixes, UI resources and partitions. This is a configured
gain difference; practical loudness still requires user verification.

`sh tests/board_audio/run.sh` passed 22,263 assertions after the expected volume
assertion first failed against 60. Official `system-update-bundle` and bundle
inspection passed. No new memory/lifecycle code changed, so the earlier sanitizer
coverage remains applicable rather than being presented as a new run. The volume
package was subsequently authorized and installed, as recorded below.

## Volume90 installation result

The user explicitly requested flashing 90. Official operation
`c2a0444c-27d4-41a4-b1cb-ccf787dd1137` installed the exact package
`d87ae02e9222d961332295aef876531504f1e3aeb08ee2116baa3dad2db49361`.
The current Recovery Boot ID's initialization-complete log was checked first.
The operation succeeded with HEALTHY on boot `16556209682536332364`, matching ELF
SHA-256 `d172e99ba688ed9f313b9655df67b13f0c4f5ca94da37fd8f65b8daa49d26fa2`
and the update commit ID. Bootloader and partition hashes remain unchanged.

Executed `iris device-status`, `iris logs --snapshot` and `iris screenshot` after
the update. The real home screen is visible, ES8311 opens successfully, and the
current boot has no observed UI error or audio output failure. Startup is silent
and retains the initial disabled Play control until a rhythm is recorded.
[Structured evidence](volume90-device-result.json) and
[device screenshot](volume90-running-device.png).

This verifies installation and initial operation. Actual loudness/distortion at
hardware volume 90 still requires the user's listening feedback. No external
audio capture or physical waveform measurement was performed.
