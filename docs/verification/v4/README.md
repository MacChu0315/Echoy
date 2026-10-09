# v4 merged-controls verification

The delivered v4 ZIP is preserved, and its HTML/README/verification/preview are
archived under `reference/v4/` with hashes. The v3 baseline is unchanged. Musical,
audio and arrangement sections were independently checked against v3 before
switching the test oracles to the approved v4 HTML.

## What changed

The home now has a one-line identity, pad/sliders at y=89, 300px feedback row,
compact status and the 84/244/84 bottom layout. Idle presents two 122x84 controls;
capture presents one 244x84 Cancel; preview/loop and their preparation present one
244x84 Pause. Hidden/inactive controls do not receive meaningful actions.

The shared C renderer evaluates CSS cubic-bezier(0,0,.58,1), per-property durations
and shortened reverse transitions. The SDK's different cubic easing is not used
as a substitute. Fixed-size tints move within a clip; eight small icon/word alpha
masks support genuine fading/scaling. Hit buttons and dynamic headings/status
remain native GSP components. Transparent initial single-button resources avoid
a flash before asynchronous glyph publication.

The reference's idle-disabled Record/Play opacity is actually 1 due to CSS
specificity; this was measured and preserved rather than silently redesigned.
The user confirmed touch/mouse acceptance. Native Tab/Enter focus mapping was not
added or counted as a passing native test.

## Actual checks

- `tools/capture_reference.cjs`: 40 exact 480x480 reference captures, 24 bilingual
  title/layout checks and two HTML-only keyboard/reduced-motion checks. The latter
  do not establish native keyboard support.
- `tools/check_home.py`: 8 native groups pass; right-end Cancel **53ms** and
  left-end Pause **45ms**, both before the 240ms transition ends. Empty/cancel
  retention, leading silence, finite preview, full-width actions, deleted top
  target and relative volume behavior are included. [Results](home-checks.json),
  [actual strip transitions/permissions](strip-events.json).
- The same home check with `--reduced-motion`: 8 groups pass, every transition
  uses `duration_ms=0`. [Results](reduced-checks.json).
- `tools/check_full_ui.py`: 6 integration groups pass, including all twelve
  expressions in Chinese/English, single-line headers and actual separator dots,
  scrolling, export, fault recovery and remove/restore. [Results](full-checks.json).
- `tools/check_desk.py`: 12 native three-tile groups pass. Existing musical,
  shared-clock, boundary update, detach, permission and PCM assertions were retained;
  new reduced-motion commands do not change musical state. The 50.01-second PCM
  has no clipping and 9,194 fully paused frames are exactly zero. All hosts/owners
  exit naturally with `late_blocks=0`. [Results](desk-checks.json),
  [PCM](audio-metrics.json), [tested binary/bundle hashes](desk-runtime.json).
- `sh tests/v4_strip/run.sh`: merged permissions, stale Cancel safety, CSS reversal,
  reduced-motion and 40 independently decoded RGBA glyph cases pass; ASan/UBSan
  pass as well.
- `sh tests/band/run.sh`: shared playback regressions pass, including actual guide
  PCM before Cancel and silence for all future cues after Cancel.
- `node tests/music/oracle.mjs`: 43,536 plans match the unchanged v4 JavaScript.
- `node tests/arrange/oracle.mjs`: 864 linked plans and 1,552 model transitions match.
- `node tests/audio/oracle.mjs /tmp/echoy-v4-audio-oracle` followed by
  `python3 tests/audio/compare.py /tmp/echoy-v4-audio-oracle`: **99/99** sound
  references pass with unchanged budgets. [Measurements](audio-comparison.json).
- `ctest --test-dir /tmp/echoy-band-model --output-on-failure`: 1/1 executable,
  all existing recording/model groups pass.
- `python3 -m unittest discover -s tests/desk_preview -v`: 12/12 pass. The timing
  fixture additionally verifies system motion changes are sent immediately and
  never issue musical or presence actions.
- An isolated actual browser loaded all three real GSP iframes, operated Record
  and whole-strip Cancel, and used system media emulation to verify reduced motion
  reaches all three native views. Screens remain 480x480, no browser script errors.
  [Checks](browser-checks.json), [desk screenshot](browser-desk.png).
- Pinned `idf_low_noise_build.py --project projects/echoy build`: ESP32-S31 build
  succeeds with **zero warnings**, firmware **1,407,088 bytes**, run
  `20261009-101935-build-2`. UI bundle: **375,888 bytes** within the 1MiB partition.

The first v4 tests failed on a v3 runtime snapshot: the old top Record still
started capture and the old pad remained at y=113. Final tests use copied runtime
snapshots, protecting the running user preview. No failing assertion was weakened
to accept old behavior; new-mode input follows real permission logs and rendered
state rather than assuming host frame waits imply backend completion.

## Visual evidence and limits

States: [idle](initial.png), [count-in](countin.png),
[capture](recording-leading-silence.png), [one reply](preview.png),
[loop](playing.png), [paused](paused.png).

Each comparison shows HTML, native GSP and a 3x amplified RGB difference:
[Pulse](pulse-comparison.png), [Voice](voice-comparison.png),
[Dust](dust-comparison.png), [Harmony](harmony-comparison.png).
Native RGB565 quantization, integer positioning and font rasterization still
produce small pixel differences; no overall similarity percentage is acceptance.

PC animation uses the official native host with `--fps 1000 --idle-poll-ms 1`
and a Linux loopback-only TCP_QUICKACK hint. This changes simulator sampling,
not the screen dimensions/pixel format or the independent audio clock. The
high rate is not a physical display performance claim. Standalone Vibe defaults
are not the v4 animation acceptance path.

The physical speaker, neighbor communication/clocks and hardware resource/timing
acceptance remain separate. No device was flashed and no remote was written.
