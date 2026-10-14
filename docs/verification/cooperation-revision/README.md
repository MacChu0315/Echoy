# Approved cooperation revision (2026-10-14)

Implements the approved rules in `../../plans/2026-10-14-cooperation-revision.md`.
The accepted sound implementation `0953c52` and its immutable package
`5d4776c9ae6d` remain preserved. No remote write or microphone capture occurred.

## Behavior

- A paused follower has one centered Play across the existing 244x84 strip;
  Record and the divider disappear. An empty/unready follower cannot start.
  Pause/Cancel still use their existing independent targets.
- Confirmed physical release immediately clears following, current source and
  membership of the former clock group. The current musical events, active PCM
  and local playing/paused permission remain. Remaining desktop descendants keep
  the detached root's copied phase; this is not three-board physical support.
- A solo replay, including after Pause, uses the existing 50 ms scheduling lead,
  rather than waiting for a historical clock's next 2.5 s bar.
- When a physically joined group is entirely paused, either side can establish
  a prompt first start. A later entrant aligns to that common bar. Clock/status
  messages never enable looping on another tile.
- Radio-only loss on a retained seam enters recovery without removing physical
  membership. A new session cancels old transactions and restores the committed
  upstream before later edits; stale proposals cannot leak through an overwritten
  ABORT. Actual magnetic release still ends the relationship independently of
  radio acknowledgement.
- Initial clock mapping may still be unavailable immediately after radio
  recovery. A copied local first-start origin is protected from old META/CLOCK
  until it can be translated, sent and acknowledged. This case was found by
  independent review, reproduced on the real two-owner scheduler, and fixed
  with a regression that first failed.

The private v2 envelope gains a bounded 36-byte `WIRE_CLOCK` message. Sequence,
local-start acknowledgement and session identity fence stale traffic. Status
refreshes every 500 ms, with changes sent promptly; it uses the existing control
lane and does not postpone transaction-control scheduling. Existing plan bytes,
CRC, two data slots, deadlines, fixed ARM/COMMIT boundaries, recording/reply
admission and per-device permissions are preserved. Both delivered devices use
this implementation; mixed-version cooperation is not newly accepted.

## Verification

All final commands below pass. Full logs remain under
`output/cooperation-revision-20261014/`; earlier failed RED/diagnostic runs are
retained separately and are not reported as final passes.

| Command | Result |
| --- | --- |
| `sh tests/cooperation/run_transactions.sh` | 26 real two-owner/transport/PCM cases; 36,969,333 assertions |
| `TRANSACTION_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' sh tests/cooperation/run_transactions.sh` | Same 26 cases; ASan/UBSan/LeakSanitizer pass outside ptrace |
| `sh tests/cooperation/run_deferred.sh` | 24 recording/reply/ownership boundary cases; 420,054 assertions |
| `sh tests/board_link/run.sh` | Protocol, physical/radio separation, CRC/retry, callback fences, bounded control/data slots and transport-window checks pass |
| `sh tests/linked_device/run.sh`; `sh tests/linked_device/run_clock.sh` | Complete upstream plans, immediate release, retained PCM, prompt repeated solo replay and clock-refresh checks pass |
| `sh tests/band/run.sh` | Band, calibration, upstream synthesis, bass and accepted speaker-profile ownership checks pass |
| `sh tests/render_active/run.sh` | 96 PCM windows bit-identical to frozen old mixer |
| `sh tests/audio/run.sh`; `sh tests/speaker_voicing/run.sh` | Actual synthesis, buffer/error/performance/extreme-pitch and accepted office-profile checks pass |
| `python3 tests/device_runtime/test_runtime.py` | 51 runtime/configuration tests pass |
| `ECHOY_RUNTIME_SANITIZE=1 python3 tests/device_runtime/test_runtime.py` | 51 tests pass with ASan/UBSan/LeakSanitizer outside ptrace |
| `sh tests/v4_strip/run.sh` | Follower permissions/centering, transitions, stale Cancel and 40 glyph masks pass |
| `sh tests/arrange/run.sh` | 864 linked plans and 1,552 transitions, normal and ASan/UBSan/LSan; only approved initial defaults and idle replay are adapted in the test oracle |
| `node tests/music/oracle.mjs` | 43,536 complete C plans match frozen music across all twelve expressions |
| `cmake -S tests -B build-host`; `cmake --build build-host`; `ctest --test-dir build-host --output-on-failure` | 2/2 tests pass |
| `python3 -m unittest discover -s tests/desk_preview -v` | 12 helpers/HTTP/launcher tests pass outside the socket-restricted sandbox |
| `python3 tools/check_cooperation_ui.py --build-dir output/build-sim-cooperation-revision --output output/cooperation-revision-20261014/native-cooperation-final` | Official GSP 1.5.1; 50 real C caption/visibility assertions and 8 pointer flows pass |
| Same command with `ECHOY_REDUCED_MOTION=1` and output `native-cooperation-reduced` | All 50 assertions and 8 flows pass |
| `python3 tools/check_desk.py --build-dir output/build-sim-cooperation-revision --output output/cooperation-revision-20261014/native-desk` | 13 native GSP/shared-C/actual PCM groups, 1,440,240 frames, zero late blocks/clipping and exact paused silence |
| `sha256sum -c SHA256SUMS` in `reference/v4` | All four frozen files unchanged |
| `python3 output/cooperation-revision-20261014/build_firmware.py` | Configured pinned-SDK `system-update-bundle` succeeds |
| `git diff --check` | Clean |

The initial sandbox rejects local sockets and LeakSanitizer under ptrace. Those
checks were rerun outside it without disabling or relaxing their assertions.
A simulator shot taken before its 240 ms transition finished showed the old
split in transit; final visual checks wait for the actual transition endpoint.
The ordinary solo-playing status line is intentionally blank, so it is not
subject to a connected-caption assertion.

[Normal native checks](native-checks.json), [reduced-motion checks](reduced-checks.json),
[complete desk checks](desk-checks.json), [actual PCM metrics](audio-metrics.json).
[Centered follower](follower-play-only.png), [independent playback](independent-playing.png).

## Preserved sound and package

[Preservation checks](preservation.json) verify byte-identical accepted
`echoy_audio.c`, `echoy_device_pcm.c`, `platform_device.c` and the entire band's
preparation/voicing/calibration region. `CONFIG_ECHOY_OFFICE_VOICING_TRIAL=y`
retains the accepted profile. Microphone/reference diagnostics remain off.
[Scene comparison](scene-preservation.json) confirms all 404 existing objects and
logical parents are unchanged; only the transparent full-width Play target is
added by the source generator. Frozen HTML is untouched.

[Package manifest](package.json): package `2b984e511b98`, ELF
`5a721bd70413aabd5716c646b5fd1d29091cffe6464e44c7d994e2923b5d5e95`.
Immutable local bundle:
`output/dual-device/echoy-cooperation-revision-2b984e511b98.irisfw`.
Its only components are partition table, `ota_0` application and UI data at
32768/2162688/15728640. The application is 2,122,368 bytes and fits the 13 MiB OTA
partition. The existing unused-factory-size warning remains; the package does
not write that application slot, the bootloader or retained Recovery.

## Physical delivery and limits

Delivery evidence is recorded below after identity, HEALTHY, retained layout,
current-boot calibration and real-home verification. Startup checks establish
delivery; they do not establish the new physically joined Play/detach behavior,
physical release latency, speaker phase offset or sustained RF/load performance.
Only two same-orientation horizontal physical devices are supported. The fixed
boundary review also injected a phase-changing CLOCK after ARM and confirmed
both commits kept the negotiated timestamp.

Both devices now run this exact package. [Delivery results](device-results.json),
[left real home](left-installed-home.png), [right real home](right-installed-home.png).

| Device suffix | HEALTHY Boot ID | Official update operation | Final observed uptime |
| --- | --- | --- | --- |
| f452a0 | 2026744546928028908 | 2650a532-634f-4e64-9334-4001b7bb20ca | 268.5 s |
| f40c6c | 9361312524343894444 | 57e470ff-9cee-442f-a4e4-b4b6c70a3666 | 159.5 s |

Both retained those new boots with zero app crashes, partial writes, write
errors, deadline misses and clipping. The nonzero PCM counter also remains
zero: startup is silent. Magnetic baselines are ready and both real homes
are normal. Old Recovery reproduced one startup watchdog reset per device;
metadata has no valid core. The official installer waited for stable Recovery
and submitted one bundle per device. That retained Recovery defect remains
unresolved; it is separate from these application changes. Existing low-level
GPIO/I2S initialization messages are retained in the current-boot logs.
