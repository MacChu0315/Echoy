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
| `sh tests/cooperation/run_transactions.sh` | 11 real two-owner transport/PCM cases, 11,389,072 assertions |
| `sh tests/board_link/run.sh` | v2 transfer, handshake, loss, readiness, supersession and stop checks |
| `sh tests/band/run.sh` | shared controller and unchanged calibration checks |
| `sh tests/linked_device/run.sh`; `sh tests/linked_device/run_clock.sh` | upstream ownership, clock refresh, staged boundaries and exact detach retention |
| `sh tests/arrange/run.sh` | 864 plans and 1,552 transitions against frozen HTML, also ASan/UBSan |
| `sh tests/render_active/run.sh` | 96 bit-identical PCM comparisons to the previous mixer |
| `python3 tests/device_runtime/test_runtime.py` | 23 lifecycle, output, cancellation, cache and copied transaction cases |
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

Package `ace57f3ff2b0` is installed on both authorized boards through the official
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

Physical interaction/listening checks are pending; the host and native results
above do not establish those observations.
