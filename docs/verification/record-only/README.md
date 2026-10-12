# Record-only empty home

The user requested hiding Play before the first playable phrase exists. The
existing 244x84 middle strip becomes one centered Record action. Role/Settings
and the overall home geometry remain unchanged. A successful recording restores
Record + Play after its one-shot reply; first Cancel or an empty recording keeps
Record only. Capture and active playback retain their Cancel/Pause controls.
The frozen HTML, musical rules and accepted speaker profile are unchanged.

## Implementation

`echoy_strip_compute` uses the current plan's note availability to select the
empty mode. Existing following/preparing/fault guards still apply. The existing
Record mask moves to the center; an independent Play opacity keeps it hidden
through empty-state transitions. A separate full-width `record_empty` native
button shares Record's callback. Cancel/Pause retains its own callback and cannot
start a new recording after returning to idle. All populated-strip transitions
retain their existing targets, timing and reduced-motion behavior.

The owned generator produces the new native control and motion ranges. Existing
split Record/Play targets keep their original geometry. No new image masks,
fonts, musical state fields or persistence are introduced.

## Preview

- Tile A: <http://127.0.0.1:3234/>
- Desktop: <http://127.0.0.1:18900/>
- A starts empty and silent. B/C are on the shelf. Older previews and recordings
  remain untouched. Do not rebuild this live bundle in place.

```sh
python3 tools/desk_preview.py --build-dir projects/echoy/build-sim-record-only \
  --no-build --no-browser --port 18900 --visual-ports 3234,3235,3236
```

## Verification

- `sh tests/v4_strip/run.sh`: the new empty-state width/divider assertion failed
  before the change. It now passes, along with following/preparing guards,
  centered Record, absent Play during cancellation/reversal, reduced motion,
  stale Cancel behavior and all 40 glyph RGBA checks.
- `tools/generate_scene.py`, the official GSP 1.5.1 / GSPC 0.6.1 `build.py` for
  `projects/echoy/pc`, and `cmake --build projects/echoy/build-sim-record-only
  --target echoy_desk --parallel`: pass without warnings.
- `python3 tools/check_home.py --build-dir projects/echoy/build-sim-record-only
  --duration-ms 55000 --output output/record-only/home`: passes Chinese/English
  empty screenshots, both ends of the full Record hit area, first Cancel, empty
  recording, leading silence, one-shot reply, returning to split controls,
  rapid Cancel/Pause within 240ms, and retention of an existing phrase.
  The same command with `--reduced-motion` also passes.
  [Normal](home-checks.json), [reduced motion](home-reduced-checks.json).
- The home checker now uses the approved role-gallery audition entry; its older
  settings audition entry was removed in the accepted settings revision.
- `cmake --build build-host` and `ctest --test-dir build-host --output-on-failure`:
  2/2 recording/display tests pass.
- `python3 tools/check_desk.py --build-dir projects/echoy/build-sim-record-only
  --output output/record-only/desk`: all native shared-language, linkage, explicit
  play/pause, preparation, detach, export and restore checks pass. Actual mixed
  PCM is nonzero when playing, unclipped and silent after pause; all three UI
  owners close normally. [Results](desk-checks.json).
- Pinned IDF low-noise `run_build_step` with
  `-B output/device-audio-ui-build build`: exit 0, warnings 0; run
  `20261010-193200-build-2`. The actual app is 1,566,944 bytes. Exact binaries and
  scene hashes are in [artifacts.json](artifacts.json); the runner's artifact
  summary points at an older default directory, so it is not used as evidence.

Native screenshots were visually checked: the single Record label is centered,
Play/divider are absent, and the side buttons retain their size. This initial verification was in the simulator. The single Record action was
subsequently installed with package `86406d956f52` on 2026-10-12; see
[installation evidence](../third-tap/device-result.json).
