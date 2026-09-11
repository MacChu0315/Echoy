# Echoy for ESP-Mosaico

Echoy is a 480×480 bilingual musical tile application for ESP32-S31 Mosaico.
Each tile composes and synthesizes its own Pulse, Voice, Dust or Harmony voice.
Four roles each have three phrases, at 96 BPM in D minor. An admitted tile
responds only to its immediate left neighbor. Separating tiles removes that
response at a bar boundary while retaining the shared beat while reachable.

The ESP-IDF application and the desktop simulator share the C music, state,
protocol, DSP and LVGL renderer. Browser playback is a PC preview of C PCM.
It does not establish that a physical speaker is working.

The fixed reference is `reference/Echoy_v5.0.3.html`, whose embedded version is
5.0.4 and SHA-256 is
`d23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5`.
See [requirements and evidence](docs/echoy/requirements-matrix.md) and
[hardware acceptance](docs/echoy/hardware-acceptance.md) for the current limits.
Full three-device physical and calibrated acoustic acceptance is not complete.

## Workspace and dependencies

Run all commands below from the **Echoy root**, not a sibling repository:

```text
Mosaico/
  Echoy/                  application, tests, tools, documents and output
  esp-mosaico-bsp/         read-only BSP dependency
  esp-mosaico-vibe/        Recovery, mosaico.py and pinned utils dependency
  esp-mosaico-claw/        reference only; its runtime is not linked
```

The checked environment uses Linux x86-64, CMake 3.22.1, GCC, Python 3.10.14,
Node.js 24.11.1 and Google Chrome 151.0.7922.75. Python image/audio validation
also uses the existing NumPy, Pillow and FreeType/fontTools environment.
Normal simulator serving uses Python's standard library. Keep the fixed
Liberation Sans/Noto Sans CJK/DejaVu font inputs and their licenses in
`assets/fonts`; `tools/generate_ui_fonts.py --check` validates their hashes.

| Dependency | Checked revision/version |
| --- | --- |
| ESP-IDF | `c712a0dde385d659a1470a136251980d31a70bc1`, version.cmake 6.2.0 |
| BSP | `3d5c451598aaffbda6f41895a0414dba4bac3505` |
| Vibe | `f1a7d4a36ccc6ccdf1c0e73dbc9fb8c9e50b2753` |
| utils | `d832e6623225bd94af67610e2a49d362ade4f000` |
| LVGL | 9.5.0, resolved in `dependencies.lock` |

The project requires **ESP-IDF >=6.2 and esp32s31**. The installed SDK is
`/home/zhuzhengming/esp/esp-idf/esp-idf-echoy-v6.2-dev`. Its Git description
still says `v6.1-dev-7651-gc712a0dde38`; the low-noise `doctor` consequently
reports a false version mismatch. Its checked `version.cmake`, component
resolver and real target builds report 6.2.0. Pin this revision; do not infer
compatibility from an SDK directory name or replace S31 with another chip.

The utils checkout must exist at
`../esp-mosaico-vibe/submodule/esp-mosaico-utils`. If absent, initialize that
repository's pinned submodule without updating its revision. Do not import
a second BSP copy. ESP-IDF resolves managed components from the committed
manifest and lock file; first-time resolution may need the official component
registry.

## Build and validate

```sh
cmake -S pc -B build-host
cmake --build build-host -j8
ctest --test-dir build-host --output-on-failure
node tools/extract_reference.mjs --self-test
node tests/oracle.mjs --self-test
node tools/capture_reference.mjs --check
python3 tests/compare_oracle.py --runner build-host/echoy_core_runner
python3 tools/generate_ui_fonts.py --check
python3 tools/generate_voicings.py --check
git diff --check
```

CTest includes music, state, protocol/clock, contact/radio, storage, diagnostics,
DSP, touch, full visual comparison, and simulator tests. The visual check uses
74 frozen screens across both languages and compares actual LVGL pixels and
hit rectangles with the fixed browser reference. Browser tools need local
process/socket permission. Do not regenerate expected fixtures to hide failures.

Use the pinned low-noise runner for firmware builds; it activates the SDK and
invokes the original `idf.py build`, preserving complete logs:

```sh
python3 ../esp-mosaico-vibe/submodule/esp-mosaico-utils/esp-mosaico-recovery/skills/idf-low-noise-build/scripts/idf_low_noise_build.py \
  --project /home/zhuzhengming/esp/ts/Mosaico/Echoy \
  --idf-path /home/zhuzhengming/esp/esp-idf/esp-idf-echoy-v6.2-dev build
```

The normal image is about 3.85 MB. It exceeds the reserved 1,835,008-byte
factory Recovery slot and fits the 14,614,528-byte `ota_0` slot at `0x210000`.
The corresponding IDF size warning is expected for this preserved layout.
The low-noise runner reports that one size warning; raw reconfiguration logs
also contain five upstream SDK CMake warnings about private include directories
shared by SDK components. They are preserved in the logs and have not been
hidden by changing the SDK or sibling sources.
**Never use the generic `idf.py flash` suggestion, overwrite factory with
Echoy, enlarge the Recovery slot, or use a system update to bypass this check.**
Ordinary `clean` and incremental rebuilds are sufficient; `fullclean` needs
separate approval and was not used for final acceptance.

To reproduce the DSP measurement artifacts locally:

```sh
mkdir -p output/generated/audio
build-host/echoy_audio_render output/generated/audio
node tools/render_audio_reference.mjs output/generated/audio
python3 tools/compare_audio.py output/generated/audio --report output/generated/audio-metrics.json
```

This produces 12 phrase and 10 isolated/probe cases, C WAVs and real Chromium
OfflineAudioContext references. Numerical agreement does not establish
speaker listening quality or acoustic synchronization.

## Desktop use

```sh
python3 tools/echoy-sim/run.py --mode reference
python3 tools/echoy-sim/run.py --mode firmware
python3 tools/echoy-sim/run.py --headless --scenario all
```

Open `http://127.0.0.1:8765`; choose another `--port` when occupied. The two
web modes are separate launches. Reference mode serves the unchanged HTML.
Firmware mode runs three local C processes and clearly labels its simulated
contacts and clock. Drag the shell grip to arrange tiles; drag the instrument
vertically to change expression. Use each tile's Play control to start it.
Language, shelf, six-step guide, fault controls and desktop volume stay outside
the product screens. Closing the browser does not send pause commands to
physical devices. See [desktop tool](tools/echoy-sim/README.md).

For the real browser interaction check and a 30-minute **PC-only** endurance:

```sh
python3 tools/echoy-sim/run.py --mode firmware --port 18887
# A second terminal:
node tools/echoy-sim/browser-check.mjs http://127.0.0.1:18887
python3 tools/echoy-sim/endurance.py --seconds 1800 --output output/sim/endurance
```

The endurance tool paces simulated time against wall time, repeatedly sends
LVGL pointer events, switches locale, captures real LVGL frames and checks C
PCM and process RSS. It deliberately labels MCU heap, DMA underruns, physical
touch, Iris mirror overhead and acoustic skew as unmeasured.

## Install and recover a physical tile

All firmware writes and Gateway lifecycle operations go through the sibling
`mosaico.py`. It accepts the external absolute Echoy project path; this was
verified in its resolver. Use the same checked SDK when operating Recovery:

```sh
export IDF_PATH=/home/zhuzhengming/esp/esp-idf/esp-idf-echoy-v6.2-dev
python ../esp-mosaico-vibe/mosaico.py --json list --details
```

Identify the actual board at operation time. A blank/ROM device may not appear
in Iris; the tool's `recover --dry-run` also checks its registered ROM endpoint.
In a restricted environment, a missing `/dev/ttyACM0` inside the sandbox is
not evidence that it is absent on the host. Do not select unrelated serial
adapters or infer identity from a cached device list.

For a blank or unverified authorized board, use
`python ../esp-mosaico-vibe/mosaico.py recover`. Once it reports **Recovery
ready**, list again and record its real Device ID and Boot ID. Then install:

```sh
python ../esp-mosaico-vibe/mosaico.py install \
  --project /home/zhuzhengming/esp/ts/Mosaico/Echoy \
  --device-id DEVICE_ID --skip-build
python ../esp-mosaico-vibe/mosaico.py --json list --details
python ../esp-mosaico-vibe/mosaico.py monitor --device-id DEVICE_ID --snapshot
```

Replace `DEVICE_ID` with the live value. `--skip-build` is appropriate only
after a successful current build. The installer checks the retained partition
layout and application identity/health. Transfer success or a reconnected USB
node alone is not acceptance. Verify **normal → Recovery → normal** using
`mosaico.py enter-recovery --device-id DEVICE_ID` and the same install command,
checking unchanged Device ID, different Boot IDs, Recovery ready and healthy
Echoy. If the endpoint disappears or the operation times out, inspect through
the same tool and restore physical connection before any further write.

The normal build disables `CONFIG_ECHOY_DEV_INJECTION`. Its Iris service
`0xec01`, method 1, is read-only diagnostics. Development-only methods and
temporary acceptance builds are described in
[hardware acceptance](docs/echoy/hardware-acceptance.md). Use the Gateway
already managed by `mosaico.py` for diagnostics and screen observation; never
open USB/serial directly. Finish any development acceptance by rebuilding and
installing the normal application, then verify `injection_enabled=false`.

## Three-tile demonstration

1. Install the same normal revision on all three individually verified tiles.
   Record each real Iris ID/Boot ID and its diagnostic radio UID/boot mapping.
   A/B/C are computer-side aliases, not firmware identities.
2. While paused, select Pulse/Voice/Dust from their Role pages. Defaults are
   expression .58/.56/.48, phrase 1 and local volume .70. Harmony initially
   selects phrase 2, Arpeggio. Each tile starts paused after reboot, including
   after saved settings are restored.
3. Start each tile separately before connecting them. Nearby discovery alone
   must not synchronize them. Establish only **one new edge at a time**;
   initially sandwiching an unidentified middle tile has ambiguous identity.
4. Wait for the real stable clock fit (at least a 20-second sample span) and a
   future-bar commit. Joining does not start a paused tile. Verify A→B→C, then
   C→B→A, duplicate roles and all Harmony variants. Do not infer success merely
   from the magnets attracting.
5. Separate a tile and check boundary removal of its response with continued
   shared time; then test pause, volume zero, output/data faults, reference
   loss and reboot. Keep all three within direct wireless reach. Use actual
   recording equipment for onset skew; software phase is not acoustic timing.

## Current limits

Physical display direction/readability, touch sensor latency, local ES8311
speaker quality, real magnetic identity, wireless distribution and calibrated
P95/max onset measurements remain subject to the recorded hardware checks.
The suggested targets remain touch ≤100 ms, UI about 30 fps, and acoustic
P95≤10 ms/max≤20 ms after propagation correction; they have not been relaxed.
The BSP exposes configured DMA depth, not exact DAC onset or a hardware
underrun counter. Diagnostics `audio.gaps` counts software catch-up events.

Saved settings are isolated in NVS namespace `echoy`; no playing state,
session, pending transaction or identity is restored. Battery read failures
preserve the last valid value; actual zero-charge shutdown and retained
`sysmeta` still require controlled hardware acceptance. English is available
through the documented configuration/development entry, with no added product
settings page. No camera, microphone, cloud account, AI runtime, arbitrary
network topology or more than three tiles are included.
