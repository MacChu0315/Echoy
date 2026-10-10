# Harmony home spacing (2026-10-10)

The user requested more open central Harmony linework during playback, with
bottom controls retaining their current sizes. Their screenshot and the retained
v4 desk showed Harmony phrase 2 at minimum density. The frozen v4 reference stays
unchanged; this is a requested refinement to the shared native C renderer.

The full 300x204 home artwork now spaces the three ribbons 44 px apart instead
of 29 px. The middle remains at local y78; the upper/lower ribbons move outward
15 px to y34/y122. Curve shape, horizontal span, colors, stroke widths and held
note animation are preserved. Idle and playing share the new layout, avoiding
a layout jump when playback starts/stops. Compact 206x101 and 73x48 role cards
retain their old spacing. Rhythm duration marks remain at their original rows.

## Validation

- `sh tests/art/run.sh`: 1,709 checks pass, including tiled surfaces and clipping;
  cached-frame/performance checks and 9,408,768 blend probes pass. The existing
  pointed-tip check was moved to the requested upper ribbon position and failed
  on the old renderer before the change.
- `sh tests/art_image/run.sh`: 2,458 checks pass; 809 QOI fixtures decode correctly
  with independent Pillow verification.
- A direct before/after render comparison covers four roles, home and both card
  sizes, idle and two playback-energy states using mask `251b`. All 33 cases
  outside Harmony home are byte-identical. Only the three Harmony home cases change;
  all differences stay above local y150, preserving duration marks below.
  [Pixel comparison](pixel-checks.json).
- The pinned GSP 1.5.1 / GSPC 0.6.1 native build passes in the separate directory
  `projects/echoy/build-sim-harmony-spacing`.
- `python3 tools/check_full_ui.py --build-dir projects/echoy/build-sim-harmony-spacing
  --output output/harmony-layout/gsp-check` passes the actual input-driven GSP
  flows: all twelve expressions, Chinese/English homes, gallery audition,
  menus/scrolling/export, simulated errors/recovery, removal and restoration.
  [Native checks](gsp-checks.json).
- The GSP geometry assertion confirms the original bottom layout: 84x84 role and
  settings buttons, 244x84 merged record/play strip, and its 122x84 halves.
- The new live desk additionally passed actual pointer-driven Harmony selection,
  playback and pause. It is left paused on Harmony phrase 2 with a playable seed5
  example, volume .65 and density .5. [Playing](playing.png), [paused](paused.png).
  These are native GSP output frames, not replacement UI images.
- `sha256sum -c SHA256SUMS` from `reference/v4` and `git diff --check` pass.
  No reference/UI scene/resource
  source or audio parameter changes are part of this refinement.

The web-oriented mechanical layout scanner reports no findings for the C file;
it does not establish Canvas geometry correctness. Native rendering, pixel
comparisons and input checks provide that evidence here.

## Live comparison and remaining review

- Retained v4: desk `http://127.0.0.1:18896/`, A `http://127.0.0.1:3222/`.
- New layout: desk `http://127.0.0.1:18897/`, A `http://127.0.0.1:3225/`;
  B/C use 3226/3227. The original running bundle was not rebuilt.

Restart the new preview, if necessary, with:

```sh
python3 tools/desk_preview.py --build-dir projects/echoy/build-sim-harmony-spacing \
  --no-build --no-browser --port 18897 --visual-ports 3225,3226,3227
```

The user has been given the new live preview for visual feedback. This layout
has not been installed on hardware; physical firmware remains `7c431dacfd59`
with the retained sound profile. No new acoustic or physical-frame-rate claim
is made by these UI-only changes.

The user subsequently accepted retaining this ribbon spacing and clarified that
the short duration marks below it needed separation. The preview on 18897/3225
now runs that follow-up build; see [the current report](../harmony-marks/README.md).
This report's comparisons describe the earlier ribbon-only change.
