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
phrase by the silent right follower. Active rejoin/shared-update checks are pending.

At the latest post-fix snapshot, both installed Boot IDs remained unchanged with
zero application crashes, deadline misses, partial writes and write errors.
`cache_busy` was 4 on the left and 10 on the right: the nonblocking publication
path was exercised on real hardware while output timing stayed healthy. These
are initial/idle-linked observations, not a sustained-playing acceptance.
[Initial retest evidence](lockfix-initial-retest.json),
[left linked home](lockfix-linked-left.png), [right linked home](lockfix-linked-right.png).
