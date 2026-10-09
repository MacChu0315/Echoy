# Echoy verification

Current v4 presentation results: [v4 controls and full regression](v4/README.md).

Earlier audible three-tile results: [audio and linkage verification](audio-linked/README.md).
The sections below retain the earlier UI/phrase evidence; their individual commands
validate that scope. The current desktop uses the shared C audio/linkage controller.

## UI and phrase verification

These checks cover the actual shared C backend in GSP 1.5.1, not scene-only
renders. They establish UI / musical-data behavior, not audio or hardware parity.
The frozen HTML and supplied verification document retain their original hashes.

## Visual comparison

Each home comparison shows frozen HTML, native GSP, and a 3x amplified RGB
absolute difference. Both use paused seed `0x44a0`, density 0.5 and phrase 3.
`tools/capture_reference.cjs` reads the unchanged HTML and crops exactly 480x480.
The browser reference uses `source: null` for a solo tile and retains `rootSeed`,
avoiding an accidental following-left status in the comparison fixture.

- [Pulse](full-ui/pulse-comparison.png), [Voice](full-ui/voice-comparison.png),
  [Dust](full-ui/dust-comparison.png), [Harmony](full-ui/harmony-comparison.png).
- Paired pages: [role](full-ui/role-comparison.png),
  [settings](full-ui/settings-comparison.png), [guide](full-ui/guide-comparison.png),
  [information](full-ui/info-comparison.png).
- [English settings](full-ui/settings-en.png), [English guide](full-ui/guide-en.png),
  [fault](full-ui/status-fault.png), [loading](full-ui/status-loading.png),
  [empty battery](full-ui/status-empty.png).

Font rasterization, antialiasing, thin lines, corners and RGB565 quantization
still differ. No overall similarity percentage is treated as acceptance.
Dynamic controls, text, scrolling and musical diagrams remain functional GSP
objects and C drawing code. The gallery uses three runtime QOI images rendered
from the same C plans; the home uses the simulator's two available Canvas slots.

## Commands and results

- `sha256sum -c SHA256SUMS` in `reference/`: both files match.
- `cmake -S tests -B /tmp/echoy-model-current`, `cmake --build /tmp/echoy-model-current`,
  `ctest --test-dir /tmp/echoy-model-current --output-on-failure`: 1/1 executable,
  ten behavior groups pass, including recording, cancellation, generated plans,
  phrase memory, boundary commits and one-shot lifetime.
- `node tests/music/oracle.mjs`: 43,536 C plans match the actual unchanged
  JavaScript, all twelve expressions, 138 density boundary values, randomized
  inputs, ordering and every represented note field; maximum 28/64 events.
- `sh tests/art/run.sh`: 1,709 checks pass, including tiled/full drawing equality.
- `sh tests/art_image/run.sh`: 2,458 checks pass; independent Pillow QOI decoding
  verifies 809 cases and all RGB565 channel levels.
- The music/art/image tests also pass ASan and UBSan (`detect_leaks=0` under the
  sandbox). This run does not claim a whole-application LeakSanitizer result.
- `python3 -m unittest discover -s tests/preview_controls -p 'test_*.py' -v`:
  6/6 pass, including private socket/file handling, actual twelve-expression
  exports, fixed HTTP paths, same-origin requests and cleanup retry.
- `python3 tools/check_home.py --build-dir projects/echoy/build-sim-next --output output/full-ui-home-check`:
  9 checks pass. Real pointer recording, leading silence, held input, richness
  locking, one-shot stop, play/pause, cancellation and volume.
  [Results](full-ui/home-checks.json).
- `python3 tools/check_full_ui.py`: five native integration groups pass:
  gallery audition stops once; all twelve expressions render; menus, scrolling,
  language and export; fault/loading/battery recovery; remove/restore. The test
  waits for C commits and runtime-image publication, not only host frame counts.
  [Results](full-ui/checks.json), [actual exported plan](full-ui/session.json).
  Both native runs finish naturally with `last_error=0`, `cleanup_error=0`, and
  no exhausted Canvas slots or rejected UI commands.
- The pinned `idf_low_noise_build.py --project projects/echoy build` reports
  `IDF LOW-NOISE BUILD: OK`, ESP32-S31, zero warnings, `echoy.bin` 1,367,520 bytes.
  Run: `20261008-105729-build-2`. The generated UI bundle is 381,328 bytes,
  within the existing 1 MiB UI partition.
- Read-only review identified the gallery's accidental looping callback and the
  stale scroll flag that swallowed a later click. Both were corrected and
  exercised in the native integration checks. Runtime-image ownership/cleanup
  and model commit timing were also reviewed.

The official interactive preview was restarted from the validated source with
`ECHOY_SIM_SEED=44a0`. An actual browser click opened the role page and displayed
its generated diagrams. The optional desk control server responds HTTP 200 and
reports a connected backend; its browser automation navigation was blocked by
the browser client, so no browser-level desk-page acceptance is claimed.

## Current remaining work

The former audio and PC three-tile gaps are now covered in
[audio and linkage verification](audio-linked/README.md). Physical speaker output,
neighbor discovery, real communication/clocks, battery/peripherals, device-side
export and hardware acceptance remain. No firmware was flashed. The approved
simulated remove action remains hidden on hardware.
