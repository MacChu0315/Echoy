# Deferred joins and shared phrase updates (2026-10-12)

The four product decisions in `../../plans/2026-10-12-cooperation-transactions.md`
are implemented. The frozen HTML remains unchanged. Its `Promise.all` preparation
and shared `ArrangeModel.stage/commit` are the reference for joint updates.

## Behavior

- A receiver already counting in, recording, preparing its recorded reply or
  playing that reply acknowledges contact immediately and defers following.
  Cancel/Pause still ends the local operation. Detach removes the deferred join;
  reattachment uses the latest valid source, not an abandoned offer.
- The recording and existing one-shot reply finish before following. Following
  never grants continuous-play permission. A device that is paused stays paused.
- Source proposals and receiver sounds prepare before a shared future bar is
  armed and authorized. Old PCM continues throughout preparation. Failure keeps
  committed banks; a failed new recording cannot audition an older prepared bank.
- Edits after ARM wait for the next revision; Pause and volume remain immediate.
  Owner acknowledgements, immutable job generations, session/connection fences,
  CRC and idempotent control messages reject obsolete work.
- Transfer/preparation waits have a 30-second ceiling. The common bar has a
  one-second negotiation lead and a 200 ms late-authorization guard. Loss during
  final authorization is detected/recovered but cannot provide perfect atomic
  delivery under arbitrary partition. No acoustic sample-alignment claim is made.
- Detach retains the current collaborative phrase. No restore-own entry or
  cross-reboot memory was added. Audio calibration, recording timing and bottom
  control geometry remain unchanged.

The home status line distinguishes connected, waiting for the left recording or
phrase, joining after recording/reply, following left, and recovery. Empty
followers no longer say that Record is needed to save their trial taps.
Examples: [waiting](waiting.png), [deferred count-in](deferred-count-in.png),
[original reply](deferred-reply.png), [following during playback](following-playing.png).

## Reproducible checks

| Command | Result |
| --- | --- |
| `sh tests/cooperation/run_deferred.sh` | 24 boundary cases, 420,054 assertions |
| `sh tests/cooperation/run_transactions.sh` | 18 real two-owner transport/PCM cases, 33,039,014 assertions |
| `sh tests/board_link/run.sh` | v2 transfer, handshake, loss, readiness, supersession and stop checks |
| `sh tests/band/run.sh` | shared controller and unchanged calibration checks |
| `sh tests/linked_device/run.sh`; `sh tests/linked_device/run_clock.sh` | upstream ownership, clock refresh, staged boundaries and exact detach retention |
| `sh tests/arrange/run.sh` | 864 plans and 1,552 transitions against frozen HTML, also ASan/UBSan |
| `sh tests/render_active/run.sh` | 96 bit-identical PCM comparisons to the previous mixer |
| `python3 tests/device_runtime/test_runtime.py` | 26 lifecycle, output, cancellation, cache contention and copied transaction cases |
| `python3 tools/check_cooperation_ui.py --build-dir projects/echoy/build-sim-cooperation-v2 --output <new-output>` | 46 real C caption/visibility assertions and 6 native GSP flows; [results](ui-checks.json) |
| `python3 tools/check_desk.py --build-dir projects/echoy/build-sim-cooperation-v2 --output <new-output>` | 13 native GSP/actual PCM checks; [results](desk-checks.json) |
| `sha256sum -c SHA256SUMS` from `reference/v4` | all four frozen files unchanged |

Boundary and transport suites also passed ASan/UBSan and LeakSanitizer outside
the ptrace sandbox. The unchanged plan codec retains its
own version; the coordination envelope is v2 and both physical boards need this
firmware. Native tests do not establish physical radio or listening acceptance.

Pinned SDK `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` and GSP/GSPC 1.5.1/0.6.1
build the system-update bundle. The known factory-size warning does not describe
the OTA target: the 2,092,160-byte application fits `ota_0` (13 MiB). The package
contains only partition table, OTA application and UI data, at offsets 32768,
2162688 and 15728640. Retained Recovery/bootloader are not replaced. Final build
log: `.codex-runs/idf-low-noise-build/20261012-172032-system-update-bundle-2890086/raw.log`.

## Device delivery

Package `ace57f3ff2b0` was installed on both authorized boards through the official
system-update workflow. Both match ELF `79e4f1701d3775e379d160ee4ce0bf3816d0b82d0fa3bb362fd6d9731b440acd`,
are HEALTHY with zero application crashes, have normal real homes and completed
separated magnetic calibration. [Machine-readable evidence](device-results.json).

| Device suffix | Boot ID | Update operation |
| --- | --- | --- |
| `f452a0` | `6968030823019921606` | `0c47f6c4-e554-45d3-bd41-cb3fdf9d336b` |
| `f40c6c` | `9645677825573746719` | `b8a34b86-6b57-490a-9f0e-17a0b2ac5877` |

The old Recovery reproduced its known startup watchdog reset on each board.
Metadata was preserved (no valid core); the installer verified a stable ready
Recovery boot before submitting one update per board. Application crashes are
zero; the Recovery issue remains separate and unresolved. Before/after bootloader
hashes match `d3cc31fc9e8f3df9c8ca3beece23fbcb97019f59cb87933266f8ddc47efadbcc`.

## Earlier interaction checks and captured output fault

The user confirmed both-empty join/first phrase without right autoplay, a complete
right recording and one-shot reply when joining during count-in, explicit cancel,
detach/rejoin while deferred, and coherent common updates with a paused right
remaining silent. However, the user reported an occasional right-side boot screen.
This is not a stable-delivery acceptance. [Reports and evidence](physical-acceptance.json).

Both devices still report the installed Boot IDs and crash count zero; neither
has a valid core dump. Each recorded one audio output deadline miss. The right
failure was captured live after contact: cumulative render/mix/conversion maxima
were unchanged at 8,916/7,624/1,430 us. At that stage pre-render owner operations and scheduling
were not timed. The later trace below establishes the captured right-side cause. The first left error fell in a
log-loss gap. No claim is made that the user's observed screen was a hardware
reboot, or that sound synthesis alone caused either deadline miss.

The next step is bounded owner timing diagnostics; keep the audio deadline,
DMA lead, musical timing and output calibration unchanged.

## Owner deadline trace (diagnosis, not a fix)

A 16-round numeric history in the existing PSRAM context records health access,
queue receive, link handling, commands, prepared-job completion, band tick/job
compile, publication, render and write. Publication records link-view cost,
mutex wait and cache-copy cost separately. On a deadline miss the current and
preceding three rounds freeze; exactly four compact lines are logged after
closing audio. Normal blocks do not print these traces. The original deadline,
DMA lead, sound and fault/recovery behavior are unchanged.

The new 80 ms fake-write-delay test first failed without diagnostics, then
verified exact lateness, phase offsets, ring wrap, copied command/transaction
metadata, four-line output only after close and absence of unexecuted stages.
All 24 runtime cases pass normally and with ASan/UBSan. Pinned SDK build passes
with the same factory-size warning; UI-data hash is unchanged.

The right device was then updated to diagnostic package `29a3cb61a5a0`, boot
`13509464675950450897`, operation `51501961-9b96-4def-9c58-ebcdba0e7863`, ELF
`83eaa386833d4ef389fe63a822d49e062b863bb422ee3a1e5fff65bc36478050`.
Its real home, healthy state, retained bootloader and separated magnetic baseline
pass. Left remains on `ace57f3ff2b0`. [Diagnostic delivery](diagnostic-device.json).
The user's first Role click reproduced the fault on return. Concatenating Iris
stdout chunks gives the complete four rows, including command/transaction fields:
[publisher-lock trace](publish-lock-trace.json). The failing owner round spent
16,806 us waiting for `view_lock` during a 17,026 us publication, exceeding its
PCM presentation time by 1,841 us. Earlier control stages totaled only 93 us,
and previous rounds completed render/write in about 10 ms. No new render was
attempted in the failing round. Boot IDs remained unchanged.

A local fix changes owner publication to a zero-wait mutex attempt; if a cached
reader is busy, old coherent snapshots stay available and the next block retries.
UI/link readers cannot stall PCM. New regression cases hold each real cached-read
path for 80 ms: both fail on the previous implementation, pass with continuous
output and immediate Pause after the fix, and receive fresh state after release.
All 26 runtime cases pass normally and with ASan/UBSan. Hardware fix validation
is pending.

The simultaneous empty-follower report is distinct: the left was in audio fault,
no complete source plan reached the right, and transaction 13 timed out before
ARM. Investigation also found same-session recovery and pre-OFFER ABORT handling
gaps. Both gaps now have regression fixes: pre-OFFER ABORT is acknowledged once,
including failure escalation and protection for deferred jobs/authorized commits.
A failed held leader candidate cannot slip through if ABORT supersedes BEGIN.
Recovery reuses existing WANT messages after owner readiness returns or a fresh
peer response follows actual silence. Matching control messages count as live
traffic. New attempts retain the 30-second limit; duplicate WANT/ABORT, unchanged
failed baseline markers and continuous heartbeats do not cause automatic retries.
The wire version and layout remain v2. Local changes/Pause while armed retain
previous behavior.

The expanded 18-case transport suite passes normally and with ASan/UBSan;
24 deferred/owner cases and protocol/calibration checks also pass. The 26 runtime
cases include both 80 ms reader-contention regressions. Independent native build
`build-sim-cooperation-lockfix` passes 46 C visibility/caption assertions and all
six GSP flows ([fresh UI results](lockfix-ui-checks.json)). Frozen v4 hashes pass.
SDK build log:
`.codex-runs/idf-low-noise-build/20261012-194345-system-update-bundle-2991222/raw.log`.
The candidate package is `f13fd6428d8b`; UI-data and retained partition hashes
match the previous installation. Hardware installation/retest is in progress.

Delivery note: first board now runs `f13fd6428d8b`, boot `7368647184006517658`,
operation `1d41206d-1d5c-461e-8ebc-a7afd7abf3a7`; idle PCM remained error-free for
90 seconds. Right-board software mode entry failed twice with device RPC error
`0x101` (`ESP_ERR_NO_MEM`), despite normal read access and unchanged Boot ID. The
helper could not allocate its 2,048-byte delayed restart task. No update bundle
was submitted. User-assisted AI-button entry to the retained Vibe Mode was
requested. Raw evidence lives in `output/dual-device/cooperation-lockfix-second/`
and `.codex-runs/mosaico/20261012T114920Z-enter-recovery/` /
`20261012T115205Z-enter-recovery/`.

## Completed delivery of the fixes (2026-10-13)

Both boards now run `f13fd6428d8b` with matching ELF
`f57805292875250767cf6f76a111a6d6a3914775438b5d90cd791768ef651420`.
The right entered retained Vibe Mode through the user's AI-button power-on,
Recovery boot `15016243081138835729`, crash count zero. A single official update
then succeeded: operation `58219d2c-cc92-4374-aeaf-7b8d6bafa456`, HEALTHY app boot
`14992168401857743263`. Left remains on its installed boot
`7368647184006517658`. [Delivery evidence](lockfix-device-results.json) includes
image and retained bootloader/layout checks. Actual homes and magnetic baselines
pass; right idle telemetry through 93 seconds has zero deadline misses or output
errors. The former software-entry allocation failure was bypassed by manual
entry; no base-firmware write or change to its helper was made.

The user confirmed the first Role click on both devices and receipt of the left
phrase by the silent right follower. Later rejoin/shared-update feedback reported
no exceptions, but slower phrase transfer and slower expression selection, as
well as pre-existing Harmony distortion. This does not close performance or
acoustic acceptance.

At the latest post-fix snapshot, both installed Boot IDs remained unchanged with
zero application crashes, deadline misses, partial writes and write errors.
`cache_busy` was 4 on the left and 10 on the right: the nonblocking publication
path was exercised on real hardware while output timing stayed healthy. These
are initial/idle-linked observations, not a sustained-playing acceptance.
[Initial retest evidence](lockfix-initial-retest.json),
[left linked home](lockfix-linked-left.png), [right linked home](lockfix-linked-right.png).

## Measured latency baseline (2026-10-13)

After intervening power-on resets (reset reason 1, zero crashes), the same f13
firmware was captured on boots `2646500753984770485` and `4969769315098002880`.
Continuous Iris logs contain a controlled left expression edit, transaction 4.
Use `event.monotonic_us`, not the ESP log prefix or host receipt time. On the left:

| Stage | Device timestamp, us | Elapsed since previous stage |
| --- | ---: | ---: |
| Proposal, 693-byte phrase | 924654506 | — |
| All phrase chunks acknowledged | 926284066 | 1.630 s |
| Both sound banks prepared | 926884739 | 0.601 s |
| Scheduled common bar | 928226095 | 1.341 s |
| Both committed acknowledged | 928334709 | 0.109 s |

Proposal-to-bar is **3.572 s**, not an exact touch-response measurement. The
earlier 15 seconds was an observation window, not a measured switch time or an
accepted response target. Selection feedback also felt slow to the user; this
capture has a 151,873 us gallery batch, a 165,764 us maximum UI callback and a
357,823 us maximum callback gap. Idle GSP frame rates are not latency evidence.

The pinned BSP dispatches application events on its 50 ms network-task loop and
has eight fixed pending-message slots. The old single in-flight slot serializes
16 phrase chunks and interleaved control traffic. The right also synthesized
and calibrated an upstream Pulse replica which carries musical/clock data but
is never played on that board. These are measured/code-confirmed optimization
targets; the bar/commit safety rules and sound parameters remain unchanged.

[Timing events and report provenance](latency-baseline.json) retain the selected
raw log events. Full bounded captures remain under
`output/cooperation-physical/20261013-latency-{left,right}-continuous.json`.
The user returned home without moving the volume slider; the right
[Harmony Arpeggio screenshot](harmony-threshold-home.png) is playing/following
with the knob around y211, approximately `(249-211)/111 = 34%`. The user reports
all expressions increasingly distort with rising volume, Answer somewhat less.
They also confirmed it persists during unchanged continuous playback at this
setting, so it is not confined to phrase-switch/start transients.
This is a separate listening issue, not an identified analog/digital cause;
the performance work does not change the retained audio profile.

## Latency candidate and local verification

The candidate defers gallery encoding until after input feedback, submits the
central image first and at most one image per timer callback, and reads external
cached state once per tick. Only real page entry/new revision sets the initial
33 ms delay; continuous hover/drag cannot postpone it. Revision replacement and
completion ownership preserve the latest update. Optional portable image timing
splits drawing, QOI encoding and submission per slot in throttled device logs.
The existing COPY ownership and error/shutdown checks remain.

Physical B is an upstream phrase/clock replica. An immutable job snapshots that
identity and omits only its inaudible synthesis/calibration; commit also releases
any old B bank even when its musical plan is unchanged. The representative
follower prepares one bank instead of two, removing 720,000 bytes of PCM arrays
and 60,000 calibration multiplications. Desktop A/B/C still prepare three real
banks. The actual 60,000-sample local Dust output matches the prior synthesis.
The physical and desktop adapters retain their separate roles; converting a
live physical B replica directly into a real desktop B is not an exposed flow.

Transport uses two chunks plus an independent priority control slot; full v2
format, CRC/decode and common-bar/authorization rules remain. Old slots remain
reserved across retransmission and reconnect until a matching callback/timeout.
Byte coverage tolerates reordered, duplicate and missing fragments. A WANT retry
uses one chunk for that transaction, including old in-flight slots, to support
older serial v2 receivers; a new transaction restores two. Existing deadlines,
backoff and lost-peer handling are unchanged. `board_link_t` grows from 53,560
to 54,696 bytes using the S31 compiler, all in the existing PSRAM allocation.
No new runtime internal-heap allocation or BSP change was added.

Verified commands and results:

- `python3 tools/check_ui_responsiveness.py --build-dir projects/echoy/build-sim-ui-response`
  (also `--sanitize`): seven scheduling regressions, 12 byte-identical expression
  encodings; hover/drag/re-entry and original three ordering cases fail before
  their fixes. Sanitized runs include leak checks outside ptrace.
- `sh tests/art_image/run.sh`: 2,458 checks and 809 independently decoded images.
  `check_full_ui.py` before/after: five native scenario groups each, with
  [36 identical final screens](latency-pixel-comparison.json).
- `python3 tools/check_cooperation_ui.py --build-dir projects/echoy/build-sim-ui-response --output output/cooperation-ui-latency-20261013`:
  46 caption/visibility assertions and [six native flows](latency-ui-checks.json).
- `python3 tools/check_desk.py --build-dir projects/echoy/build-sim-ui-response --output output/latency-desk-20261013`:
  [13 real-PCM native checks](latency-desk-checks.json) pass on the final UI source,
  including independent Pause, old sounds during preparation and exact detach
  retention. Captured PCM is nonzero when expected, unclipped and silent after Pause.
- `sh tests/band/run.sh`: existing band/calibration tests and six new upstream
  bank-lifecycle cases (60,201 assertions); new cases also pass ASan/UBSan/LSan.
  `sh tests/render_active/run.sh`: 96 bit-identical actual PCM comparisons.
  `sh tests/linked_device/run.sh` and `run_clock.sh`: pass.
- `python3 tests/device_runtime/test_runtime.py`: all 26 cases pass, including
  the previous nonblocking-reader and Pause regressions.
- `sh tests/board_link/run.sh`: protocol and new window cases pass normally and
  with ASan/UBSan. The 693-byte model improves 2.000 to 0.900 s; reordered/missing
  fragments repair in 5.550 s on the new receiver and 4.850 s on the old serial
  receiver. These are deterministic host results, not radio measurements.
- `sh tests/cooperation/run_transactions.sh`: 18 cases / 33,039,014 assertions;
  `sh tests/cooperation/run_deferred.sh`: 24 cases / 420,054 assertions. Both pass
  normally and with ASan/UBSan. No common-bar, guard or timeout was relaxed.
- Frozen `reference/v4/SHA256SUMS` and `git diff --check`: pass.

Pinned SDK `system-update-bundle` succeeds. The known factory-size warning remains:
the 2,097,584-byte application targets the 13 MiB `ota_0`, not Recovery. Offline
official inspection verifies only partition table, OTA application and unchanged
UI data; the retained layout hash matches. [Candidate identity](latency-artifacts.json):
package `e44a3d8919d3`, ELF
`f5fdd9f1a1e3e5167228456303547fa887b19a5004483f293ddbc51d52afa144`.
The low-noise runner's printed `projects/echoy/build/echoy.bin` is an old default
path; the actual build and packaged binary are in `output/device-audio-ui-build`.
No hardware latency improvement is claimed before installation and measurement.

Delivery preflight: after the user paused/separated both boards, one software
Recovery-entry attempt per device returned `0x101` (`ESP_ERR_NO_MEM`). Both then
reported their normal f13 Boot IDs with empty queues and no install operation or
submitted package. Manual AI-button entry resolved delivery. Evidence:
`output/dual-device/cooperation-latency-{first,second}/entry-failed-status.json`
and `.codex-runs/mosaico/20261013T030826Z-enter-recovery/` /
`20261013T030957Z-enter-recovery/`. This is the existing Recovery-entry allocation
problem, before executing the candidate firmware.

Both official installations subsequently succeeded. [Device results](latency-device-results.json)
record HEALTHY/identity, retained bootloader/layout and separated calibration.
Left boot: `13804741210610604633`; right boot: `3888807882828278489`. Both Recovery
boots and application boots have zero crashes. Actual homes were inspected.

The first comparison failed before phrase transport: both sides logged correct
magnetic contact, but no peer attachment or transaction proposal occurred. The
right logged repeated type-2 broadcast send failures starting at 153,844,923 us,
with free internal heap 1,939 bytes after role-gallery use (9,931 at startup).
Left recorded seed `1092`, four anchors, and has 4,355 free internal bytes.
Both retain their new Boot IDs and zero audio deadlines/write errors. Memory and
radio-send causes are being investigated; no root cause or hardware latency
improvement is claimed. Scene evidence is preserved at
`output/cooperation-physical/latency-join-failure-20261013/`, with full bounded
logs in `20261013-latency-new-{left,right}-continuous.json` beside it.

## Retained image storage follow-up

[Allocation evidence](image-storage-diagnosis.json): the real encoder produces
7,499 bytes for the empty Dust gallery (2,315 Voice + 3,636 Dust + 1,548 Harmony),
accounting for 94% of the observed 7,992-byte internal-heap decrease. The four
opaque Chinese strip glyphs total 9,760 bytes. GSP COPY retains encoded sources
after completion and while their page/bind is hidden. With the installed
16 KiB malloc threshold, these small persistent copies prefer internal memory.
Dust is already the smallest gallery; cycling roles would not be a useful
reversible way to release these sources on the affected firmware.

`echoy_platform_t.image_storage_alloc` now optionally supplies malloc/free-compatible
retained-image storage. The device requests only `SPIRAM | 8BIT`, with no internal
fallback. One UI helper consumes each encoder buffer, copies only its used bytes
into that storage, frees the temporary and submits public GSP TAKE ownership.
PC directly transfers the encoder buffer. Immediate failure releases it in UI;
accepted data is released by GSP on replacement/cancellation/shutdown, independently
of completion. Gallery and all four bottom glyphs use the same path. Encoders,
pixels, global allocator configuration, sound and transport are unchanged. Encoder
scratch allocations still use ordinary temporary malloc; their peak remains an
explicit hardware observation item.

The generic peer `ERROR` also concealed the specific driver return. The pinned
SDK's multi-header error-table generation omits ESPNOW entries; the application
now registers the single public `esp_now.h` using `idf_define_esp_err_codes`.
The already-linked `esp_wifi` SDK component becomes an explicit private header
dependency. No new external component, SDK/BSP edit, runtime code/allocation or
send behavior is introduced. The final binary contains all nine specific ESPNOW
error names. Prior logs still cannot establish a numeric NO_MEM result.

Validation:

- `check_ui_responsiveness.py --sanitize`: seven scheduling cases, twelve
  byte-identical encodings and ten ownership cases, with ASan/UBSan/LSan.
  New ownership cases fail on the previous COPY implementation.
- `python3 tests/device_runtime/test_runtime.py`: 27 pass, including explicit
  PSRAM caps and allocation failure without fallback. Caption assertions: 46 pass.
- Official native `check_full_ui.py` on `build-sim-image-storage`: five scenario
  groups and normal cleanup pass; [36 final screens remain pixel-identical](image-storage-pixel-comparison.json).
  [Native results](image-storage-ui-checks.json).
- After building its separate `echoy_desk` target, `check_desk.py` passes all
  [13 real-PCM checks](image-storage-desk-checks.json). The first invocation found
  the missing executable before starting; it was built and the full check then ran.
- Pinned-SDK `system-update-bundle` passes with the same factory-size warning.
  The 2,098,016-byte app fits OTA; UI data and retained layout are unchanged.
  `git diff --check` and candidate source hashes pass.

[Package identity](image-storage-artifacts.json): `c4e5fb0e45d2`, ELF
`512a572fb11b73ca14800433b05309aa08458c97d601017369ea93babf3b071b`.
Both official installations succeeded with zero application crashes, retained
bootloader/layout, normal homes and separated magnetic calibration:
[delivery results](image-storage-device-results.json). Left boot
`2491134233307376902` has 12,235 free internal bytes at empty home; right boot
`16801241995940483221` has only 2,315, despite the identical image. The user then
joined the default empty homes without entering Role. Both peer sessions formed,
but the right reported explicit `ESP_ERR_ESPNOW_NO_MEM` on type-8 sends:
[selected events](image-storage-no-memory.json). Image-source placement is fixed,
but total internal headroom and physical performance are not accepted.

The next investigation checked actual S31 sizes: UI container 37,128 bytes,
art workspace 145,848, model 14,256 and view cache 14,764 (410 entries). No single
object matches the observed 9,808-byte PSRAM difference; a net heap difference
alone does not establish which allocation moved. UI creates a complete standby
model even with both external read/action callbacks, although every relevant
state/action/plan path delegates to that controller. The bounded follow-up removes
only this unused allocation and gives the pure UI cache an explicit PSRAM allocator.
Standalone and single-callback behavior remain. Startup-only object/heap diagnostics
will verify placement, rather than inferring it from totals. Band models remain
unchanged: the PCM owner reads hot snapshots and copies a 7,072-byte current plan
every 10 ms, so moving those models would require separate deadline evidence.

The UI-only follow-up now removes the standby model exactly when both read and
action callbacks exist, preserves zero/single-callback initialization and seed
behavior, and retains the old cache creation wrapper. A custom cache allocator
receives the full 14,764-byte body, zeroes it and never falls back after failure.
Images and cache share `ui_storage_alloc`; small text allocations are unchanged.
Startup-only `ECHOY_UI_STORAGE` / `ECHOY_MODEL_STORAGE` diagnostics record actual
object placement and internal free/largest blocks, including explicit skipped
versus failed model construction. Band allocation and all musical rules remain.

Verification on this final source:

- `check_ui_responsiveness.py --sanitize`: 16 construction cases, seven scheduling
  cases, ten image-ownership cases and twelve byte-identical encodings pass;
  constructor red cases distinguish the former redundant model and default-heap
  cache allocation. Independent review/re-run also passes.
- `python3 tests/device_runtime/test_runtime.py`: 27 pass. Target S31 compilation
  of the three changed UI/cache/platform objects has no warnings.
- `check_cooperation_ui.py` with `build-sim-ui-cold-storage`: 46 caption assertions
  and [six real three-tile native flows](ui-cold-checks.json) pass; zero late PCM
  blocks and normal TAKE/UI/host shutdown. Critical screenshots were inspected.
- Full pinned SDK `system-update-bundle` and offline inspection pass, retaining
  the same UI-data/layout hashes. Only the known factory-size warning remains.

[Follow-up package](ui-cold-artifacts.json): `150bd97add2b`, ELF
`09644dd9013d660b1237b2ae016f6d4f3637a99313d8e8988d5f2e7c91b2163d`.
Right-only hardware installation is awaiting manual Vibe Mode entry, with the
left on c4 as the initial comparison. Internal-memory recovery and measured
physical switching latency remain unverified.
