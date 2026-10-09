# Single-device firmware preparation

Scope: local implementation and verification of the device entry, audio adapter,
battery adapter and task lifecycle. No physical device was opened, reset, probed
or written during this work. The accepted v4 reference remains unchanged.
This records the preparation phase; the subsequent authorized installation and
real-device failure are in [the hardware record](../device-install/README.md).

## Verified result (2026-10-09)

- `tests/board_audio/run.sh`: 22,259 assertions passed, including its ASan/UBSan
  and default LeakSanitizer run.
- `tests/board_health/run.sh`: passed exhaustive 16-bit SOC/signed-current and
  validation/lifecycle checks; sanitized run passed without diagnostics.
- `tests/device_runtime/test_runtime.py`: 14/14 passed both normally and with
  ASan/UBSan/default LeakSanitizer.
- Shared band, v4 strip/glyph checks and the model CTest passed.
- Official native GSP checks: [8 home groups](home-checks.json) and
  [6 full-page groups](full-ui-checks.json) passed in an isolated build.
- Pinned ESP32-S31 build passed with **zero warnings**, run
  `20261009-115821-build-2`. Final application is 1,553,552 bytes; UI data is
  375,696 bytes. The official bundle target and independent archive inspection
  passed. No partition or dependency revision changed.
- Compiler `-fstack-usage` inspection found no application-local frame above
  3,552 bytes after moving the 7 KiB empty plan off task stacks. This excludes
  cumulative callees, SDK frames and live stack usage; runtime watermarks remain
  a required hardware check.
- `reference/v4/SHA256SUMS` and `git diff --check` passed. Pinned BSP/utils
  working trees remain clean.

Frozen local bundle:
[`echoy-single-device-38b79718eed8.irisfw`](../../../output/device-release/echoy-single-device-38b79718eed8.irisfw),
1,090,156 bytes. SHA-256:
`38b79718eed8f835b979b9e4341ea414fc9bbcfb356d58294ac75dcabd21fe5d`.
[Artifact manifest](artifacts.json) records component hashes, offsets, dependency
pins and source hashes. The official packer pads the 3,072-byte partition table
to a 4,096-byte sector with `0xff`; the packaged hash was checked accordingly.

The user selected `/dev/ttyACM0`. OS-only `stat`/sysfs inspection confirmed
Espressif `303a:4002`, USB name `ESP-Iris Mosaico woofly`. No serial session was
opened. Protocol identity, current partition layout and Recovery compatibility
must be checked after target-specific authorization and before installation.

## Implementation and limits

- GSP and firmware share the same UI, musical model, arrangement and synthesizer.
  The single physical tile exposes A; B/C remain absent. Hardware hides the
  approved simulation-only removal action.
- The audio/control task owns the band. Synthesis transfers immutable jobs through
  queues. UI calls never wait for synthesis or I2S; their view mutex protects only
  a short copy. Periodic diagnostic output runs on the low-priority worker.
- PCM uses public ES8311/IDF interfaces with 24 kHz mono 16-bit output, three
  120-frame DMA descriptors and a 40 ms overall write deadline. Partial accepted
  frames are counted and never replayed. Pause/cancel clear DMA. Output failures
  require explicit recovery and recovery never resumes a phrase automatically.
- The estimated presentation timeline advances by accepted samples, not by write
  return timestamps. It is **not** a measured DAC playhead or a hardware underrun
  counter. Speaker latency, pitch, gain, noise and performance require measurement.
- The codec facade in the pinned SDK drops some interface errors and has an
  ES8311 enable-reference failure edge. Public interface proxies retain the first
  transport failure, allow driver accounting to complete with PA off, and return
  failure to the application. Host tests use its actual public headers and
  substitute the hardware/codec calls, including these documented failure paths.
- Battery reads validate readiness, presence, raw SOC and freshness, preserving
  I2C errors instead of interpreting them as zero. Unknown battery and IMU values
  are shown explicitly. No microphone recording is added.
- Worker completion requires a second atomic acknowledgement after the FreeRTOS
  completion signal returns. This prevents freeing task-owned synchronization
  objects during their final access on the second CPU. Cleanup errors/timeouts
  remain visible and retain the affected handle.

## Reproducing local checks

Run from the Echoy repository:

```sh
sh tests/board_audio/run.sh
sh tests/board_health/run.sh
python3 tests/device_runtime/test_runtime.py
sh tests/band/run.sh
sh tests/v4_strip/run.sh
cmake -S tests -B /tmp/echoy-device-model
cmake --build /tmp/echoy-device-model
ctest --test-dir /tmp/echoy-device-model --output-on-failure
```

The adapter and runtime suites compile production C with host I2S/I2C/FreeRTOS
substitutes. They cannot establish physical peripheral behavior. Sanitized
variants use `BOARD_AUDIO_TEST_CFLAGS`, `ECHOY_HEALTH_SANITIZE=1`, and
`ECHOY_RUNTIME_SANITIZE=1`; the local LeakSanitizer runtime needs execution outside
the restricted ptrace sandbox.

The isolated native GSP build is `projects/echoy/build-sim-device`. Commands:

```sh
python3 tools/check_home.py --build-dir projects/echoy/build-sim-device --output output/device-home-check-native
python3 tools/check_full_ui.py --build-dir projects/echoy/build-sim-device --output output/device-full-ui-check
```

Both use the official GSP 1.5.1 host with actual C callbacks. The standalone
backend's audio service remains simulated; these checks establish UI regression
coverage, not hardware sound. Local loopback sockets require execution outside
the restricted network sandbox. The live `build-sim-v4` bundle was not rebuilt.

Use the pinned ESP-IDF environment and GSPC from `../../dependencies.json`.
Firmware uses the existing official `idf_low_noise_build.py --project
projects/echoy build` runner. Packaging uses the official CMake target:

```sh
idf.py -C projects/echoy -B projects/echoy/build \
  -D ESP_IRIS_PYTHON=/home/zhuzhengming/.local/state/esp-mosaico/runtimes/esp-iris/07c754d5785b861b/py3.10/bin/python \
  system-update-bundle
```

`esp_iris.py bundle inspect <bundle>` independently loads the resulting archive
and validates its manifest/component hashes. This local unsigned development
bundle has no cryptographic publisher signature; do not describe hash validation
as signature verification.

## Installation boundary

The update manifest targets partition table `0x8000`, application `0x210000`
(`ota_0`) and UI data `0xF00000` (`ui_apps`). Updating UI erases its 1 MiB partition;
the update also changes boot-selection/result metadata. It includes neither the
bootloader nor the retained factory Recovery image. Actual target layout and
Recovery compatibility still require device identification.

Do not run generic `idf.py flash` or `app-flash`: their generated default app
offset is the factory Recovery address `0x20000`; official Mosaico guards reject
them. After target-specific user confirmation, use official `mosaico.py
--workspace . iris system-update --bundle <reviewed-bundle> --device-id
<confirmed-id>`. No installation command was executed here.

Physical single-device acceptance must precede two/three-device linkage work.
Wireless neighbor/clock integration, actual acoustic and power behavior, IMU and
hardware export remain unfinished.
