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

## Fix round 1/5 — review corrections (base `5417e5d`)

Status: DONE. All seven requested fixes are implemented and verified. The
hardware/integration/Recovery constraints above remain; this section supersedes
the original software validation metrics.

### Root causes and changes

1. **Paused graphics:** the reference also inherited `.channel-muted` from the
   removed desktop wrapper. The new regression failed with Canvas opacity `1`
   instead of `0.45`, independently of the previous accent check. The harness
   now preserves the muted class and computed Canvas opacity before detaching,
   checks both contexts afterward, and regenerated all 74 captures. Original
   HTML/hash unchanged. Native paused/waiting instruments and every score dot
   use opacity 115/255; playing restores 255. A second real-render RED caught
   the fact that ordinary LVGL `opa` does not attenuate this custom drawing:
   `opa_layered` is required to composite the complete plotted image once.
2. **Capture lifetime:** route, blocked state, loading mode, modal visibility
   and target-device changes invalidate the previous interaction domain.
   An active expression gesture emits exactly one finish; volume capture ends
   without another volume event. Subsequent move/up/cancel cannot reach the
   hidden control. Regression paths cover activity→boot/status/modal/role and
   volume→boot/status/modal-close/role transitions.
3. **Visual acceptance:** full-image metrics remain, with no exclusion masks.
   Added 122 independent critical-region error/coverage/ink-energy checks and
   962 pixel-measured component/score bounds. Region MAE <=12, foreground
   coverage within 2px >=65%, ink-energy ratio 0.50–1.65; filled boundaries and
   all visible score-dot bounds must be within 2px. The modal's visible top
   instrument portion is checked; opaque dialog pixels are never treated as
   visible underlying instrumentation. Uniform one-level scrim quantization is
   accounted for by measuring structural foreground against each image's own
   local background; absolute pixel errors still include every pixel.
4. **Waiting:** transport icon and text now use the source `#8b622f`.
5. **Volume:** measured numeric width, 6px gap and percent width form one
   centered group; the percent is no longer approximately 18px too far right.
6. **Selected role:** the accent follows the entire rounded lower edge,
   replacing the isolated 67px center line.
7. **Bottom navigation:** separate icon/text labels use the authored 9px gap;
   their vertical positions moved down to match the browser's painted bounds.

### Regression evidence

- `test_ui_view opacity` failed on the original full-bright property; the
  layered-opacity variant independently failed before the compositing fix.
- `test_ui_view capture` failed because the route transition did not emit the
  one required activity-finish event. Both tests now pass, alongside the
  original snapshot/locale/modal/object-reuse checks.
- Four intentionally broken images (deleted 342×218 plot, deleted all role
  cards, play button moved 8px with unchanged touch metadata, wrong full-bright
  plot) were each accepted by the old whole-frame check, producing four saved
  RED failures. All are rejected by the new image-region/structure/bounds gates.
- Targeted tests against the saved `5417e5d` actual PNGs fail all five checks:
  waiting color, percent position, selected lower corner thickness, role-nav
  vertical bounds and play-nav vertical bounds. New actual PNGs pass all five.
- These guards and detail checks are invoked by the new `ui_visual` CTest,
  which first renders fresh images. The earlier three UI CTests remain intact.

### Final verification

- Host configure/build: PASS.
- `ctest --test-dir build-host -R ui --output-on-failure`: **4/4 PASS**
  (the original 3 plus the new visual-regression test).
- `ctest --test-dir build-host --output-on-failure`: **16/16 PASS**, 19.02s.
- Standalone `python3 tests/visual_compare.py --cases tests/ui_cases.json
  --renderer build-host/echoy_ui_runner`: **74/74 PASS**.
- **1,084 regional/painted-boundary checks PASS**, maximum painted-boundary
  error **1px**; the existing **232 actual touch rectangles** still pass.
- Regional foreground coverage minimum **98.3093%**; observed ink-energy ratio
  range **0.91947–1.09159**. Wrong full brightness is not hidden by a global MAE.
- Mean whole-image MAE **1.845843/255**; worst **3.876412/255**. RGB565-adjusted
  mean **1.583417/255**; maximum >24-channel-error pixel fraction **3.964844%**.
- **No comparison masks.** All actual images and amplified diffs were replaced
  with current outputs; both complete contact sheets were regenerated. A single
  affected-screen inspection batch (`output/ui/round1-affected.png`) compares
  paused/playing Pulse, waiting Voice, English Harmony, selected Pulse/Harmony
  role pages, and Chinese/English volume against their corrected references.
  Confirmed the fade, color, centered percent, rounded selection edge and
  corrected button text baseline; no extra polishing iteration was used.
- Chrome `--generate` and `--check`: **74/74 PASS** for each.
- Font `--check`: **231 glyphs**, regular/bold and generated output PASS.
- Added `generate_ui_cases.py --check`: **74 frozen input projections PASS**.
- Oracle self-test: **48,600 music cases / 216,510 events**, **6 trajectories /
  21 checkpoints**, **74 UI cases**, HTML SHA-256 unchanged.
- Final `idf.py build`: PASS; **3,154,720 bytes (`0x302320`)**. No compiler
  warning; the expected reserved-factory size WARNING remains (overflow
  `0x142320`), while `ota_0` is large enough. No partition or flashing change.
- `git diff --check`: PASS.

Logs, including each saved RED, are in `output/ui/validation/round1/`.
Lower-priority curve antialiasing and speaker-line fidelity were not expanded
in this round. Physical frame rate/latency, layered-draw memory cost and
concurrent audio/UI load still require Task 10; control-owner wiring remains
Task 8. No sibling repository changes, remote write or subagent delegation.
