# Background home artwork (2026-10-13)

The user explicitly approved the design in
`../../plans/2026-10-13-responsive-home-rendering.md`. The expensive 300x204 home
art now runs on a private producer; GSP remains the only physical touch reader
and UI owner. The 15px feedback row, buttons, gallery and musical action queue
retain their existing paths. No synthesis, output gain, recording timing,
wire format or persistence behavior changed.

The producer copies immutable frame/plan requests, coalesces ongoing animation,
and rejects completed results from obsolete structural generations. Two RGB565
buffers, private workspace and a 12288-byte task stack use explicit PSRAM.
The UI submits with `esp_gsp_canvas_try_push`; successfully borrowed storage
stays immutable until release. Release never touches a deleted task. Failed
stop, flush or destruction retains live storage for retry. Host and firmware
use the same producer logic, with native pthread / public WithCaps adapters.

A dedicated `home_art` visibility layer prevents an old role from appearing
when a page opens before its new frame is ready. Same-role animation/capture
keeps the preceding completed frame until the next arrives. Once GSP has
accepted a frame, generation changes do not retract or prematurely free it.
This is background frame preparation, not a claim of 30 distinct complex-art
frames per second or measured finger-to-sound latency.

## Verification

- `sh tests/art_producer/run.sh`: native and ESP fake branches, seven groups
  each. Covers blocked 200ms raster work, immutable/coalesced input, structural
  changes, queue failures, borrowed/released buffers, shutdown races and every
  startup allocation/task failure. ASan/UBSan/float-cast-overflow and default
  LSan pass outside the ptrace runner.
- `python3 tools/check_ui_responsiveness.py --build-dir
  projects/echoy/build-sim-capture-quality` and `--sanitize`: 10 scheduling,
  12 QOI, 10 image ownership, 17 creation and 11 real-worker integration cases.
  Includes tap timestamp/volume handling while raster work is blocked,
  old-role suppression, non-starving animation, final static frames,
  publish errors and retryable `release_closed`.
- Existing art/image tests pass (1722 / 2458 checks, 809 decode fixtures).
  The prior full 3712-frame / 99,716,368-pixel equivalence corpus has no
  differences. Additional monotone x scanning reduces one Voice frame's
  executed segment checks from 100800 to 7640; its regression fails on the
  old code. Paired PC Voice rendering is about 15% faster versus `73058`.
- Matching official GSP native build: five full-UI groups, ten recording/input
  groups, six cooperation groups with 46 caption assertions, and thirteen
  real-C audio/desktop groups. All exit without UI/cleanup errors; desk PCM
  checks retain silence after pause and have no late blocks. Results are
  [ui](ui-checks.json), [input](input-checks.json),
  [cooperation](cooperation-checks.json), and [desk](desk-checks.json).
- Twenty-five static Chinese/English home screenshots match the previous
  synchronous version exactly: [comparison](static-home-comparison.json).
- Pinned-SDK `system-update-bundle` build passes, final run
  `20261013-201632-system-update-bundle-2`. The sole warning is the existing
  unused factory-partition size check; the inspected package targets OTA and
  preserves the retained bootloader/layout. Frozen HTML hashes and
  `git diff --check` pass.

Two local integration failures were investigated and corrected before install:
the resource-only `art` bind rejected visibility with error 261, so the generated
layer now owns visibility and test fakes validate the actual compiled capability.
The cooperation script clicked Record 32.6ms before the UI enabled it after
detach; it now waits for actual Record permission, as Play already did. No
product permission rule or timeout was weakened. Failed evidence remains in
`output/background-home-full-ui-20261013` and
`output/background-home-cooperation-20261013`.

## Hardware evidence and limits

Both authorized boards run package `09275b935ad8`, ELF
`8c180bbaf1b34e6cf2e2dd1ca961c5a78347ffcbbd125b90a2ef647cc8cd4d5d`.
The [package manifest](package.json) and [startup records](device-startup.json)
retain exact identities, operations and inventory hashes. Actual homes, healthy
boot and separated magnetic calibration pass. Installation used the official
workflow and preserved Recovery; no remote repository writes occurred.

The left recorded six user-counted Voice taps, retained all six (`seed=1954`),
and the user said immediate response seemed better. There were no same-grid
merges, read errors, diagnostic drops or overwrites. All 34 observed controller
IRQs were consumed in separate reads; the previous counted case had 32 IRQs,
19 reads and 13 multi-IRQ windows. Latest-IRQ-to-read maximum was 46506us and
raw-to-observer maximum 238us. See [counted-recording.json](counted-recording.json).
The 228328us maximum inter-read gap includes quiet periods with no IRQ and is
**not** input latency. These are different hand-played sequences, not a controlled
percentage speedup or proof of all fast-tap reliability.

The user confirmed Play/Pause, continued silence and then a normal Harmony
home. No restart, audio deadline miss, partial write or write error appeared.
The worker minimum unused stack was 9200 bytes after Voice and 3648 after
Harmony; internal free after that check was 25271 bytes. GUI callback gaps in
recording/preview windows were around 60–70ms maxima; these are not pure frame
render timings. Complex Harmony raster work still reached 145215us in the
worker, demonstrating why it must not run on the touch/UI owner.

A same-version physical cooperation check is pending: silent initial following,
both playing, right pause, then a left expression update while right stays
silent. Acoustic Harmony quality remains open and its output profile is unchanged;
the separate exact-plan/clean-level evidence is in `../harmony-stages/README.md`.
