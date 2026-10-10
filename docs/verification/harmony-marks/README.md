# Separate Harmony duration marks (2026-10-10)

The user accepted retaining the expanded upper ribbons and clarified that the
remaining crowding was the short horizontal duration marks below them. This
change applies to those marks on the full-size Harmony home canvas only.

## Cause and display rule

Live preview A used Harmony phrase2, seed `0005`, density .0777778. Its anchor at
step2 lasts .23 s, followed by a .20 s accompaniment at step3. The old display
mapped them to x45–70.76 and x62.5–84.9, so the two strokes overlapped. Musical
overlap is valid; painting overlapping translucent strokes caused visual merging
and made the result depend on event order.

Each mark retains its onset position and maximum duration-based length. On the
home canvas its end is capped five pixels before the next included onset, leaving
visible space after accounting for rounded line ends. Event order is not assumed,
and hidden accompaniment cannot shorten capture-only anchor marks. Very short
spans clamp to the onset rather than drawing backwards. Music data is not mutated.

The upper 44 px ribbon spacing, mark rows, colors, multi-tone row counts and bottom
button geometry remain unchanged. Compact role cards and the other roles retain
their previous rendering. The frozen HTML and audio behavior are unchanged.

## Verification

- `sh tests/art/run.sh`: 1,722 checks pass, plus cached-frame/performance checks
  and 9,408,768 blend probes. New regression checks cover the actual overlapping
  durations, all three chord rows, event reordering, plan immutability, hidden
  capture layers and 32 separate marks at half-step spacing. Five assertions
  fail on the old renderer and pass on the new one.
- The same art suite passes ASan/UBSan/LSan outside the known ptrace sandbox.
- `sh tests/art_image/run.sh`: 2,458 checks pass; independent Pillow decoding
  confirms all 809 QOI fixtures.
- A 54-plan corpus covers six seeds, all three Harmony expressions and densities
  0/.5/1. The original renderer merges marks in 35 cases; the new renderer has
  one distinct first-row stroke for every event in all 54 cases. In seed5 phrase2,
  low/mid/high density now shows all 6/9/12 marks instead of 5/6/5.
  [Corpus evidence](corpus-checks.json).
- Before/after comparison preserves all 33 cases outside Harmony home. In the
  three changed home states for seed `251b`, only pixels at local y154–155 change;
  upper ribbons are byte-identical. [Pixel evidence](pixel-checks.json).
- The separate native build `projects/echoy/build-sim-harmony-marks` passes.
  `python3 tools/check_full_ui.py --build-dir projects/echoy/build-sim-harmony-marks
  --output output/harmony-marks/gsp-check` passes all twelve expressions,
  Chinese/English homes, gallery audition, menus/export, recovery and restoration.
  Its geometry assertion retains the 84/244/84 px bottom layout and 84 px height.
  [GSP checks](gsp-checks.json).
- Actual pointer input in the live GSP desk verifies Harmony playback and pause.
  The capture waits until the playback clock is past the scheduled start, so it
  shows sounding playback rather than the waiting-for-boundary state.
  [Playing](playing.png), [paused](paused.png), [playback state](playing-state.json).
- `sha256sum -c SHA256SUMS` in `reference/v4` and `git diff --check` pass.

## Separation preview

The new build replaces only this task's previous preview at the same URLs:
desk `http://127.0.0.1:18897/`, A `http://127.0.0.1:3225/` (B/C 3226/3227).
The previous example and controls were saved before restart. The new A view was
prepared with the same seed5 and density .0777778, paused on Harmony phrase2.
The original v4 desk at 18896 and all real devices were left untouched.

```sh
python3 tools/desk_preview.py --build-dir projects/echoy/build-sim-harmony-marks \
  --no-build --no-browser --port 18897 --visual-ports 3225,3226,3227
```

The user reported improvement and requested the label alignment below. Hardware
still runs `7c431dacfd59`; this UI change
has not been installed on the board and does not claim hardware timing acceptance.

## Align with slider label tops

The user found the separation improved and requested the short marks lower, at
the top ink pixels of the Chinese Volume/Simplicity labels, approximately level
with Dust's marks. The labels' actual ink begins at screen y263, below their y258
layout boxes. Harmony home marks move down 20 px: local row centers 155/161/167
become 175/181/187. With the canvas at y89, the first stroke's visible top is y263.
Three-row chords occupy screen y263–276, matching the labels' ink height. Dust's
existing mark center remains y262; its rendering is unchanged.

The five-pixel endpoint gaps, note positions, upper ribbons, card previews and
bottom controls are preserved. The position-sensitive art assertions fail nine
checks on the previous renderer and pass after the translation. `sh tests/art/run.sh`
passes all 1,722 checks plus cache/blend checks; `sh tests/art_image/run.sh` passes
2,458 checks and 809 independently decoded fixtures. The isolated native build
passes, and GSP API geometry retains the original bottom-control bounds.

Before/after comparison confirms an exact 20 px translation of the Harmony mark
region, with no changes to the upper ribbons or any of the 33 other sampled
role/size/state cases. [Translation checks](height-pixel-checks.json).
Actual GSP pointer flows cover playback after its scheduled start, pause, and all
three Harmony expressions. Native screenshots measure both label tops and the
first mark top at y263 in all four captured states.
[Pixel alignment](height-alignment-checks.json), [native flows](height-native-checks.json),
[playing](aligned-playing.png), [three-row chord](aligned-short-chord.png).

The current height-adjusted preview uses `build-sim-harmony-mark-height`: desk
18898, A/B/C 3228/3229/3230. The prior preview at 18897/3225 remains running to
preserve the user's newly recorded rhythm; the new preview contains a separate
seed5 Harmony example. Hardware remains on the retained audio package.

```sh
python3 tools/desk_preview.py --build-dir projects/echoy/build-sim-harmony-mark-height \
  --no-build --no-browser --port 18898 --visual-ports 3228,3229,3230
```

The user accepted retaining this alignment. It remains a simulator-only change;
hardware has not yet received these UI refinements.
