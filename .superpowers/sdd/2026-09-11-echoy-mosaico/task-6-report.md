# Task 6 report — shared LVGL product UI

Status: DONE_WITH_CONCERNS. Base: `ca64519`. Branch: `feat/echoy-mosaico`.

## Delivered

- Shared native LVGL 9 perform, role, status, boot and volume surfaces. MCU
  startup uses the shared boot surface; PC renders the same C implementation.
- Four palette-accurate instrument plots ported from `DuijuView.draw()`: pulse
  rings/accent ticks, seven voice curves/notes, 135 deterministic dust grains,
  three harmony strands and actual event-duration bars. All 16 score steps
  read the copied bar plan's full motif. Pause retains the static score/shape.
- Snapshot-based current versus pending names, four roles and each role's three
  phrases, saved Harmony phrase 2, all product feedback branches, low battery,
  loading/role preparation/error/retry/cancel and full bilingual UI copy.
- Relative/direct expression formulas, pointer capture/end/cancel semantics,
  overshoot clamping, independent volume, modal hit priority and key isolation,
  immediate pressed states, return-route preservation across loading.
- Reused object pools; no per-tick object-tree rebuild. Internal LVGL timer
  targets 34 ms; plots use live phase/hit energy, Harmony breathes while held,
  and progress updates existing objects. Enqueue-only sink plus copied snapshot
  and actual hit-time APIs for Task 8. No file/radio/NVS/codec work in UI lock.
- Eight pinned subset font sources, licenses, original/subset SHA-256 metadata,
  generation tool, 231-glyph regular/bold coverage, 24 display sizes plus
  monospaced variants. `⠿` was actually missing from the six reference face
  files; fixed with pinned DejaVu Sans symbol faces rather than a placeholder.

## Cross-task reference repair

The original Task 1 harness measured styles while `.screen` inherited
`--accent` from `.unit`, then moved `.screen` directly under `body` before
screenshotting. This made the status LED, expression fill/marker, volume thumb,
boot symbol and pending-track color transparent. A local CDP computed-style
query confirmed empty inherited accent and transparent fills after detaching.

Added a before/after palette assertion. `node tools/capture_reference.mjs
--check` failed on the first case with actual `''`, expected `'#d95535'`.
Only after reproducing this was the computed accent explicitly set on the
screen before detaching. The parent approved this bounded Task 1 repair.
The HTML SHA-256 remains
`d23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5`.
Chrome regenerated and independently checked all 74 images, manifest and
capture metadata. Palette-context assertions now accompany each capture.

## Validation and evidence

- Touch RED: initial no-op implementation failed the first edge-hit assertion.
  GREEN: unit test now checks button edges, formulas, off-screen dragging,
  cancellation, modal penetration, quick return, role/phrase options and status
  actions. A separate added boot-block test failed before the blocking fix.
- View tests: current/pending labels, unchanged snapshot across 100 locale
  changes/updates, object-count stability, low battery, preserved role page
  after loading, modal keyboard/transport isolation, and boot invisible-action
  prevention. These tests were added during implementation; this report does
  not claim an independently observed RED for every individual visual branch.
- `cmake -S pc -B build-host` and `cmake --build build-host -j8`: PASS.
- `ctest --test-dir build-host -R ui --output-on-failure`: 3/3 PASS.
- `ctest --test-dir build-host --output-on-failure`: 15/15 PASS, 12.68 seconds.
- `python3 tests/visual_compare.py --cases tests/ui_cases.json --renderer
  build-host/echoy_ui_runner`: 74/74 PASS, strictly 480×480 PPM/PNG. Every visible
  label passes allocated-height validation. 232 unobscured control rectangles
  are verified against actual pixel-by-pixel hit grids and <=2 px bounds.
- Final mean pixel MAE: **2.180181/255**; worst: **3.876412/255** (`en/error-data`).
  Mean after RGB565 allowance: **1.905751/255**. Maximum fraction of pixels with
  any channel difference >24: **3.985677%**. **No masks**.
- 74 actual PNGs, 74 amplified difference PNGs, 2 all-case contact sheets and
  per-case numeric metrics are in `output/ui/`. A complete batch was reviewed;
  localized typography/battery/progress differences were corrected together,
  then the full bilingual contact sheets were checked once for confirmation.
  The initial renderer smoke render exposed an incorrectly reset surface size;
  this was fixed before the complete batch. Parent also reviewed four actual
  full-size results independently. Native LVGL has no web detector to run.
- `node tests/oracle.mjs --self-test`: PASS, 48,600 music cases, 216,510 events,
  6 session trajectories, 21 checkpoints, 74 UI cases, reference hash unchanged.
- `node tools/capture_reference.mjs --generate` and `--check`: PASS, 74 cases,
  Google Chrome 151.0.7922.75; no network requests or remote writes.
- Activated `/home/zhuzhengming/esp/esp-idf/esp-idf-echoy-v6.2-dev/export.sh`, then
  `idf.py build`: PASS, final image **3,154,208 bytes / 0x302120**. No compiler
  warning; one app-partition-size WARNING (see below).
- `git diff --check`: PASS.
- Persistent logs: `output/ui/validation/`, including the actual accent RED,
  reference generate/check, final CTest, oracle, visual comparator and IDF build.

## Concerns / next task contract

1. Task 8 must wire the existing UI sink to the real control-owner queue and
   publish snapshots plus actual output-hit times. Task 6 startup intentionally
   stays on boot until that owner supplies real lifecycle/readiness state. No
   fabricated role/audio-ready acknowledgment or duplicate core owner is added.
2. The binary fits `ota_0` (`0xdf0000`) but exceeds the reserved factory Recovery
   slot (`0x1c0000`) by `0x142120`. IDF reports one warning. The partition table
   is unchanged. Use the approved Recovery→ota_0 path; do not run the generic
   factory-offset `idf.py flash` command printed by IDF.
3. 30 fps and <100 ms physical touch feedback remain hardware acceptance items;
   timer cadence and immediate in-process pressed-state updates are software
   evidence only. Audio/UI concurrency, RAM peaks and device readability are
   not measured here.
4. Pixel thresholds measure aggregate image error with explicit RGB565 allowance.
   <=2 px validation proves control-hit boundaries, not every glyph outline.
   FreeType/LVGL alpha rasterization and browser font metrics remain visible in
   difference images. Font pack uses explicit SC CJK face and 4-bit alpha; no
   missing CJK glyph or whole-page mask is used to hide differences.
5. Font generation needs the documented Pillow/fontTools versions; pinned font
   subset sources and C output remain in-repo. Original system fonts are only
   required for deliberately re-pinning the corpus, not ordinary builds/checks.

No sibling repository changed; no remote write, flash, deployment or subagent.
