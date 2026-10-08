# Echoy for ESP-Mosaico

A shared-C implementation of the frozen HTML in `reference/`. The PC desk runs
three official ESP-GSP simulators with actual synthesized sound, twelve musical
expressions, linked phrases, independent play/pause and a common sample clock.
The 480x480 GSP screens support Chinese and English. The immutable HTML remains
the design and behavioral reference; it does not generate the native desk's sound.

[Current state](docs/PROJECT_STATE.md) · [Verification](docs/verification/README.md)

## Audible three-tile desk

Run from this repository after resolving the pinned app dependencies:

```sh
export GSPC_EXECUTABLE="$PWD/../.toolchains/gspc-0.6.1/gspc"
python3 tools/desk_preview.py
```

Open `http://127.0.0.1:18896/`. A starts on the desk, B/C on the shelf.
Click the example button or press Record, wait two beats, then tap during four
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
Normal use does not continuously record audio. An explicit `--control-dir` must
already exist, belong to the current user and have mode 0700; otherwise a private
temporary directory is created.

## Firmware and diagnostic UI

Versions remain pinned in [docs/dependencies.json](docs/dependencies.json).

```sh
export IDF_PATH="$PWD/../.toolchains/esp-idf-7b9cc1a"
export GSPC_EXECUTABLE="$PWD/../.toolchains/gspc-0.6.1/gspc"
export GSP_SIM_EXECUTABLE="$PWD/../.toolchains/gsp-sim-1.5.1/sim"
python3 ../esp-mosaico-vibe/submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/echoy build
```

The portable audio/linkage sources compile for the pinned ESP32-S31 SDK.
The physical speaker, device discovery/transport, wireless clocks and real
peripherals are not connected yet. The firmware remains a UI integration build;
compilation does not establish physical sound or hardware acceptance. No device
write is part of these commands.

The earlier `mosaico.py --workspace . project sim --project projects/echoy
--interactive` entry remains a **single-screen UI-only diagnostic**. Use the desk
entry above for audible/multi-tile acceptance. Its optional legacy controls in
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

Follow [AGENTS.md](AGENTS.md) and preserve [third-party notices](THIRD_PARTY_NOTICES.md).
