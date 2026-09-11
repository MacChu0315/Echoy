# Echoy shared product UI

`components/echoy_ui/view.c` is the shared LVGL 9 implementation. The MCU
adapter starts the BSP display and shows the product boot page. The PC runner
uses the same view, text, fonts, plan drawing, and hit testing. It does not
load reference PNGs or embed browser-rendered pages.

## Owner integration

All `echoy_view_*` functions are UI-thread-only. On the MCU the caller must
hold the BSP display lock; the internal 34 ms LVGL timer already runs in the
LVGL execution path. The timer updates the existing plot and progress object.
There are 112 reusable rectangle slots and 112 reusable label slots; snapshots
and gesture updates change these objects without rebuilding the object tree.

Task 8 must connect the real control-owner queue:

- Obtain `echoy_ui_view()` after `echoy_ui_start()`.
- Attach an enqueue-only sink with `echoy_view_set_sink()`.
- Publish copied `echoy_snapshot_t` values using `echoy_view_update(view, snapshot,
  device_index)`. The snapshot includes the committed bar plan and full motif.
- Publish actual audio-output hit events and their monotonic microsecond times
  with `echoy_view_hit()`. Do not report look-ahead scheduling as an audible hit.
- Set `echoy_view_locale(view, english)` without modifying the product snapshot.

The sink receives `echoy_ui_event_t`: play, role, phrase, activity, finish
activity, volume, cancel loading, recovery, and navigation. It must enqueue;
no codec, file, NVS, network, or core-owner mutation belongs in this callback.
The UI does not synthesize asynchronous role-ready or play-ready acknowledgments.
The control owner retains responsibility for fault recovery and transport intent.

Relative expression dragging uses `start + (start_y - current_y) / 180`.
The direct scale uses `(338 - current_y) / 150`. Both clamp to `[0,1]` and
end on release/cancel. Volume uses its independent horizontal track. The modal
has its own hit priority; Enter cannot trigger the obscured transport, arrows
change volume, and Escape closes it. Loading preserves the requested route.

## Font provenance and regeneration

`assets/fonts/manifest.json` pins the original source hashes, subset font
hashes, glyph corpus, and conversion package versions. The eight subset sources
include Liberation Sans, Noto Sans CJK, DejaVu Sans Mono, and DejaVu Sans symbols,
in regular and bold weights. DejaVu Sans supplies the Braille dust icon which
was missing from all six originally listed reference faces. Font licenses are
stored alongside the source subsets. The generated C contains 4-bit alpha masks
at the product's display sizes; it is never hand-edited.

```sh
python3 tools/generate_ui_fonts.py --check
# Only when deliberately updating the pinned source/corpus:
python3 tools/generate_ui_fonts.py --pin
python3 tools/generate_ui_cases.py
```

The font check verifies every visible capture character, Canvas caption,
regular/bold coverage, source hashes, and Unicode literals in the product view
and translation files. The UI projection changes language without changing
role, phrase, transport, session, or pending state.

## Host verification

```sh
cmake -S pc -B build-host
cmake --build build-host -j8
ctest --test-dir build-host -R ui --output-on-failure
python3 tests/visual_compare.py --cases tests/ui_cases.json --renderer build-host/echoy_ui_runner
ctest --test-dir build-host --output-on-failure
node tests/oracle.mjs --self-test
```

The 74 frozen cases use a 480×480 LVGL RGB888 framebuffer. Each render writes
PPM, PNG, a raw hit grid, and a 4× amplified absolute-difference PNG. Raw PPM/hit
grids are ignored by Git. The comparator checks every unobscured reference
`data-action` rectangle against actual hit-test results, allowing at most two
pixels at boundaries. It also measures MAE before/after the per-channel RGB565
quantization allowance, the fraction of pixels with channel error over 24,
and verifies that every rendered label fits its allocated height. No mask is
used. Aggregate pixel thresholds are MAE <= 8 and outlier fraction <= 9%; these
are not a claim that every glyph outline matches within two pixels.

The screenshots, differences, contact sheets, metrics and validation logs are
under `output/ui/`. The reference and the renderer are always separate inputs.
The runner uses reduced motion and fixed clock/motif/locale/hit state; hardware
uses the LVGL timer and live snapshots/hits.

Task 6 fixed a reference-capture defect: detaching `.screen` from `.unit` lost
its inherited `--accent`. The harness now preserves the computed value on the
screen before detaching it, and checks the value before/after. The accepted
HTML bytes remain unchanged. All 74 reference images were regenerated and
checked with the pinned Chrome version.

## Hardware limits

No hardware frame-rate, touch-to-feedback latency, simultaneous audio/UI load,
or font readability measurement has been made. Those are Task 10 checks.
The 34 ms timer is a target cadence, not a measured 30 fps claim.

The Task 6 image is 3,154,208 bytes (`0x302120`). It fits `ota_0` (`0xdf0000`)
and exceeds the reserved Recovery `factory` partition (`0x1c0000`), producing
one IDF size warning. Do not enlarge or overwrite factory to hide this warning.
Use the approved Recovery-to-application update path; the generic `idf.py flash`
message targeting `0x20000` is not the project's deployment workflow.
