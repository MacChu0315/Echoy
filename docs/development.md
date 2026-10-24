# Developing Echoy

This branch prepares a local **1.2.1 / Iris 0.2 migration candidate**. See
[migration scope and device approval gate](iris-0.2-migration.md). It is not
a replacement for the accepted v1.2.0 firmware until physical validation.

Run all commands below from the **Echoy repository root**, not this `docs/`
directory. The [v1.2.0](releases/v1.2.0.md) source freezes the accepted
phrase pickup and UI fixes, retaining the v1.1.0 three-device repair and local
recording-reply work. Firmware publication on GitHub is pending; [v1.1.0](releases/v1.1.0.md)
remains the published download. Historical `echoy-three-basic-ba951963754a` and
`echoy-dual-accepted-20261014` tags remain immutable. Unmerged experimental features are excluded from this version.

The examples assume the original workspace layout: Echoy beside a compatible
`esp-mosaico-vibe-iris02` checkout and the pinned `.toolchains/` directory. A clone of
Echoy alone does not install that SDK, BSP, GSP compiler, simulator or host audio
tools. Follow the official Mosaico workspace setup and resolve the versions in
[dependencies.json](dependencies.json) before running these commands. This README
update does not claim a new clean-machine build or simulator test.

A shared-C implementation of the approved v4 HTML in `reference/v4/`. The PC desk runs
three official ESP-GSP simulators with actual synthesized sound, twelve musical
expressions, linked phrases, independent play/pause and a common sample clock.
The 480x480 GSP screens support Chinese and English. The v4 home uses one
merged Record/Play strip that becomes full-width Cancel or Pause, with an
interruptible 240ms transition and the browser system reduced-motion preference. The immutable HTML remains
the design and behavioral reference; it does not generate the native desk's sound.

[Project home](../README.md) · [Project state](PROJECT_STATE.md) · [Verification](verification/README.md)

## Audible three-tile desk

Run from this repository after resolving the pinned app dependencies:

```sh
export GSPC_EXECUTABLE="$PWD/../.toolchains/gspc-0.6.1/gspc"
python3 tools/desk_preview.py
```

Open `http://127.0.0.1:18896/`. A starts on the desk, B/C on the shelf.
Click the example button or press Record, wait for the three-second countdown, then tap during four
beats. One reply follows and stops. Drag B/C beside A, wait for the seam to
confirm, then press each tile's Play to join. Moving apart preserves phrases and
play permission; pause affects only the selected tile. Language is shared across
the desk, as in the prototype. Timing cues never enter the recorded rhythm.

The launcher uses the installed `/usr/bin/paplay` and existing PulseAudio output;
it does not change system volume or install packages. JavaScript handles the
outer desk geometry and controls; synthesis, arrangement and scheduling run in C.
Each official GSP screen has its own owner thread. No scene screenshot replaces
live controls. Native per-tile views use ports 3222/3223/3224.

The launcher owns its child processes and stops them on Ctrl+C. Use fresh ports
and an alternate `--build-dir` for concurrent work; never rebuild an active
preview's bundle in place. `--headless --silent` is for generated-PCM tests;
`--capture path.wav` explicitly records the output adapter until the desk stops.
Normal use does not continuously record audio. `ECHOY_REDUCED_MOTION=1` forces
instant visual changes for testing; otherwise the desk follows the browser system
preference. The approved v4 native input acceptance scope is touch/mouse. An explicit `--control-dir` must
already exist, belong to the current user and have mode 0700; otherwise a private
temporary directory is created.

## Firmware and diagnostic UI

Versions remain pinned in [dependency pins](dependencies.json).

```sh
export IDF_PATH="$PWD/../.toolchains/esp-idf-7b9cc1a"
export GSPC_EXECUTABLE="$PWD/../.toolchains/gspc-0.6.1/gspc"
export GSP_SIM_EXECUTABLE="$PWD/../.toolchains/gsp-sim-1.5.1/sim"
python3 ../esp-mosaico-vibe-iris02/submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/echoy build
```

The portable audio/linkage sources compile for the pinned ESP32-S31 SDK.
The v1.2.0 code retains speaker output, fixed Flash contact calibration,
complete horizontal-neighbor admission and the three-device coordinator. Joined
startup is supported. Targeted recording/following and ABC→BCA were confirmed
on hardware; this is not exhaustive RF/acoustic qualification. A recorded root
can reply locally before background activation when peers are not looping.
No device write is part of these build commands.

The earlier `mosaico.py --workspace . project sim --project projects/echoy
--interactive` entry remains a **single-screen UI-only diagnostic**. Use the desk
entry above for audible/multi-tile and v4 animation acceptance. The Linux native
adapter uses loopback TCP_QUICKACK plus a 1000Hz simulator tick to avoid per-RPC
frame stalls; this is not a claim of a 1000Hz physical display. C presentation
updates run at 20ms intervals and audio uses its independent sample clock. Its optional legacy controls in
`tools/preview_controls.py` also remain diagnostic-only.

## Verification and source generation

```sh
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
node tests/music/oracle.mjs
node tests/arrange/oracle.mjs
sh tests/audio/run.sh
sh tests/band/run.sh
sh tests/v4_strip/run.sh
sh tests/desk_runtime/run.sh
python3 -m unittest discover -s tests/desk_preview -v
python3 tools/check_desk.py --build-dir projects/echoy/build-sim-desk
```

`check_desk.py` snapshots the binary/bundle into its own temporary runtime,
uses three official hosts and real pointer input, captures PCM and checks natural
cleanup. Build the chosen directory first. Browser audio comparison is separate:

```sh
node tests/audio/oracle.mjs /tmp/echoy-audio-oracle
python3 tests/audio/compare.py /tmp/echoy-audio-oracle
```

The browser oracle uses the frozen HTML and local Playwright/Chrome; see the test
script options for installations on another machine. UI-only regressions remain
in `tools/check_home.py` and `tools/check_full_ui.py`. Scene regeneration uses the
recorded fonts, fontTools, Pillow and CairoSVG: `python3 tools/generate_scene.py`.
Edit the generator rather than its JSON/view-key outputs.

Follow [AGENTS.md](../AGENTS.md) and preserve [third-party notices](../THIRD_PARTY_NOTICES.md).


## Build-time fixed audio

Firmware enables `CONFIG_ECHOY_PREBUILT_AUDIO` by default. A native host C
compiler (`cc`, `gcc` or `clang`, selected by `ECHOY_HOST_CC`) runs the unchanged
production synthesizer during the build. Three timing/tap cues and four complete
audition phrases are embedded as little-endian float32, together with the original
eight audition windows per role. The selected office/device gain table and
speaker profile are part of the resource contract. Changes to the generator,
synthesizer, gains or headers regenerate the binary and its hash manifest.

Startup validates the resource and copies it into ordinary owned PSRAM buffers;
there is no filesystem cache, persistent-data migration or runtime synthesis
fallback. Invalid resources, mismatched profiles and allocation failures stop
sound preparation through the existing failure path. Random selection, recorded
phrase synthesis, pickup and UI behavior remain unchanged. The resource adds
1,000,932 bytes of embedded data; with PSRAM rodata enabled, the immutable copy
also consumes PSRAM in addition to the existing owned playback buffers.

```sh
sh tests/audio/run_fixed_audio.sh
```

This compares every sample/window against the original C synthesis for office,
device and native reference profiles, rejects corrupt resources, exercises failed
allocations and runs the existing random-audition regression through the fixed
path. `tools/check_iris_bundle.py` verifies the actual app contains the generated
bytes and that the input hashes still match the source tree.

For native simulator coverage, configure `projects/echoy/pc` with
`-DECHOY_PREBUILT_AUDIO=ON`; it embeds the native reference profile and uses the
same loader. The default native build remains the synthesis reference. Firmware
can disable `CONFIG_ECHOY_PREBUILT_AUDIO` for comparisons. The opt-in
`CONFIG_ECHOY_VERIFY_PREBUILT_AUDIO` diagnostic regenerates all seven assets on
the ESP and checks sample errors and window positions before PCM activation;
leave it disabled in ordinary builds and startup measurements.

## Phrase pickup regression

The local pickup layer follows the frozen audition reference in
`reference/opening/`. It does not change relationship or recording plans.
Firmware enables `CONFIG_ECHOY_PHRASE_PICKUP` by default. For an original-mode
comparison, disable that option or append `sdkconfig.pickup-original.defaults`
in a fresh build directory. Keep `sdkconfig.office-voicing-trial.defaults` and
`CONFIG_ECHOY_OFFICE_VOICING_TRIAL=y` in both configurations.

The native desk retains original playback by default; set
`ECHOY_PHRASE_PICKUP=1` to exercise the added layer. Existing page layout and
random center audition are unchanged. Use separate build directories and native
ports when another development session is running.

```sh
sh tests/pickup/run.sh
python3 tests/pickup/compare_retained.py --baseline 8d8ae5f --output output/pickup-reference
python3 -m unittest discover -s tests/device_runtime -v
ECHOY_PHRASE_PICKUP=1 python3 tools/check_three_chain.py --build-dir output/build-sim-pickup --output output/pickup-native
```

Build the native directory before running its check. `tests/pickup/run.sh` covers
HTML event/mix parity, actual device PCM, published transitions, original
v1.1.0 audition samples/random state/input guards, and the real three-owner early
reply (including failure, Pause and already-playing peers). It checks pickup
artwork against the bank currently sounding, independently of group readiness.
These checks do not establish speaker listening or physical wireless acceptance.
