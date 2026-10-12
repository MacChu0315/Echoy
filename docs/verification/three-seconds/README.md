# Three-second popup and preparation countdown

The user requested the popup to disappear after three seconds and recording
preparation to begin at “预备-3”. The preparation display now reads 预备-3 → 预备-2
→ 预备-1 with one-second steps. English reads Ready · 3/2/1. The original 60 ms
scheduling lead is retained: guide cues occur 0.06, 1.06 and 2.06 seconds after the
Record event, and the distinct capture-start cue is at 3.06 seconds. Recording
then retains its four 625 ms beats and original 2.5-second capture window.

`echoy_model.h` defines shared preparation timing for the model, UI and cue
scheduler. Guide text now says three seconds; `AGENTS.md` records this user-approved
change to the earlier two-beat rule. Musical events/quantization, output profile,
persistence and the frozen reference remain unchanged. No migration is needed.

## Verification

- Added model timing, real-PCM cue and native popup-expiry assertions all failed
  against the previous version before production changes.
- `cmake --build build-host` and `ctest --test-dir build-host --output-on-failure`:
  2/2 pass. Recording tests cover the 3.06-second start boundary, ignored early
  taps, unchanged four-beat window, leading silence, cancellation, stale work,
  parameter boundaries and persistent musical clock. Existing test timelines
  were moved past the longer preparation window without weakening assertions.
- `sh tests/band/run.sh`: scheduler and calibrated-bank tests pass. Actual C PCM
  contains exactly seven guide/capture cues at the specified onsets, with the
  distinct 1500 Hz start cue and 1050 Hz guides; all other samples are silent.
  Cancellation checks now cover six seconds, including every later scheduled cue.
- `python3 tests/device_runtime/test_runtime.py`: 22/22 pass, including FIFO
  Record/Cancel, DMA clearing, activation gating, failure and shutdown paths.
- `sh tests/v4_strip/run.sh`: permissions/motion, stale Cancel and 40 RGBA cases pass.
- Official GSP 1.5.1 / GSPC 0.6.1 `echoy_backend` / `echoy_desk` builds pass.
  `python3 tools/check_home.py --build-dir projects/echoy/build-sim-three-seconds
  --duration-ms 70000 --output output/three-seconds/home` and its `--reduced-motion`
  variant pass. Checks verify three-second popup expiry, all three displayed
  countdown frames, no early capture, the observed 3.06-second start, unchanged
  recording/empty/cancel/one-shot/Play/Pause behavior and both locales.
  [Normal](home-checks.json), [reduced motion](home-reduced-checks.json).
- `python3 tools/check_desk.py --build-dir projects/echoy/build-sim-three-seconds
  --output output/three-seconds/desk-isolated`: passes all shared-controller,
  real PCM, linkage, language, pause, export/restore and quiet-tail checks.
  [Results](desk-checks.json).
- The first desk run was concurrent with both native home suites and failed its
  strict `late_blocks=0` assertion once at 42.52 seconds. All tiles were already
  paused, with no pending/preparing work; scripted actions ended around 28 seconds.
  The identical binary passed when rerun alone, with zero late blocks. No timing
  limit, test or scheduler implementation was relaxed. This suggests host scheduling
  jitter but does not establish its exact cause or guarantee operation under load.
  Original evidence remains in `output/three-seconds/desk`.
- Pinned SDK `system-update-bundle` build: exit 0, warnings 0, run
  `20261012-103038-system-update-bundle-2`. The actual application is 1,568,016 bytes;
  package `9ef37e48c8b0` preserves the retained layout and contains the updated UI.
  [Exact artifacts](artifacts.json) are recorded independently of the runner's
  stale default-build artifact summary.

Native screenshots of 预备-3, 预备-2 and 预备-1 were visually inspected.

## Physical installation (2026-10-12)

Official `mosaico.py iris system-update --bundle ... --device-id ... --endpoint ...`
installed `9ef37e48c8b0` on the same Mosaico, operation
`ddd0b874-0860-4676-b5d2-3762bfe27524`. Recovery boot `13720273676645676443`
completed initialization before the one update submission. New application boot
`14075077765010003089` reports HEALTHY with the expected image identity, commit
and unchanged bootloader/partition-table hashes. [Evidence](device-result.json).

`iris device-status`, `iris screenshot` and `iris logs --snapshot` confirm the
same new boot in normal mode after 77 seconds, crash count zero and the expected
[Record-only home](installed-home.png). No UI/audio write/deadline errors were
observed at startup; musical bank trims and PCM gain remain unchanged. The known
GPIO ISR/I2S initialization messages remain, without preventing healthy startup.
Physical acceptance of the new countdown and popup timing is pending.
