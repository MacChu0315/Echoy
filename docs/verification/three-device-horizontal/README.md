# Horizontal three-device cooperation (2026-10-15)

The approved behavior extends the accepted two-device version to at most three
same-orientation horizontal boards. A/B/C denote physical left-to-right order,
independent of device IDs. All three active devices now run the verified package;
physical three-board acceptance remains pending. All local release gates pass; the exact
delivery candidate is recorded in [candidate.json](candidate.json).

Source `f324cd9` (owner foundation `6ff0f00`); package `7c6e37e7be4d`, ELF
`33acbd2b0cbde1b6948af3ce6136f7aa546fd8a1a6938dc4c87d051467ee9016`.
Immutable bundle: `output/three-device/echoy-horizontal-v3-7c6e37e7be4d.irisfw`.
Application 2,125,136 bytes, retained OTA capacity 13,565,952 bytes; partition/UI
component bytes exactly match the accepted dual package. Office voicing remains
enabled; reference/microphone/magnetic diagnostics are disabled. Kconfig omits
the hidden Harmony-only trial when office voicing is selected; generated JSON
and C definitions confirm it is disabled.

## Implementation

`echoy_group.c` owns a bounded three-member group on the link worker. Each device
retains its actual independent band, preparation jobs and PCM owner. B derives a
complete candidate from A and sends its own prepared result to C; the physical
leftmost member collects individual readiness and activation acknowledgements.
Only complementary, same-orientation horizontal magnetic seams admit members.
Public routed reliable unicast carries nonadjacent controls; clocks are sampled
over each actual adjacent seam. Radio/sensor/codec work stays off the PCM owner.

Six persistent node-wide transport slots include retired destinations, with two
reserved control slots. Receivers require complete byte coverage and CRC before
decoding. Group/boot/session/generation/transaction and owner publication checks
reject abandoned work. An attempt has a 30-second total bound, including
superseding edits; failed preparation/transport retains the committed sound.
Physical release immediately ends the old membership; radio loss retains it and
uses bounded recovery. The coordinator and cold physical context use explicit
PSRAM; existing zero-wait audio publication and stop-failure ownership remain.

Continuous playback requires each device's own Play. Any first Play starts the
current phrase promptly; later entrants join the shared bar. A locked musical
update keeps its timestamp, even if that shortens a newly started first cycle.
Changed and unchanged playing banks align at that boundary. Paused devices stay
silent. A recording result completed after ARM is retained for the next attempt;
its one-shot reply finishes before a deferred upstream is admitted.

The private radio protocol is v3. Mixed v2/v3 peers do not establish following or
commit, and local playback remains usable with existing recovery feedback. All
members of a physical test group must run the same candidate. No persistent-data
migration, UI layout/wording change, SDK/BSP edit or new production dependency.

## Local verification

[Normal commands/results](host-results.json) and
[sanitizer commands/results](sanitizer-results.json) include their original logs.
`source-hashes.json` identifies the unchanged main/native sources tested by both.

- Five group suites: real owner fixed-boundary PCM, transport primitives, active
  physical adapter, independent native adapter and three independent owners.
  Three-owner cases include all six ID orders and both join orders; single/pair
  release, A/B/C-first and simultaneous starts, READY edits, fixed overlap,
  stale owner publication, delayed/lost/duplicate data/control, unmapped clocks,
  clock refresh, radio loss/renewal, loading/preparation recovery, incompatible
  peers, first empty recording and deferred middle-node recording/reply.
- Historical dual regressions: 26 real transport/band/PCM cases with 36,969,333
  assertions and 24 deferred cases with 420,054 assertions. Their explicit v2
  fixture is `tests/board_link/v2_baseline.inc`, excluded from firmware; active
  v3 behavior is covered separately above.
- Existing linked-device/clock, band, sound, speaker voicing, strip/glyph,
  magnetic diagnostics, desk runtime, reference music/arrangement and CTest pass.
  This includes 864 linked plans/1,552 state transitions, 96 bit-identical PCM
  comparisons and 51 device-runtime cases. CTest passes 2/2.
- ASan/UBSan/LeakSanitizer pass for all five group suites, historical
  transaction/deferred cases and all 51 device-runtime cases. The earlier
  sandboxed arrangement run could not execute LSan under ptrace; the unchanged
  check passes outside ptrace in `final-host-arrange.log`. No suppression or
  relaxed assertion/deadline was used.
- `reference/v4/SHA256SUMS`, accepted synthesis/arrangement/PCM/voicing/UI source,
  UI scene/default configuration hashes and saved dual bundle were rechecked.

Independent review reproduced candidate export from an older upstream revision;
a failing regression now verifies transaction/revision ownership before export.
Further failing regressions cover lost STATE near a retry boundary, capture after
ARM, stale clock ACK after PCM commit, loading recovery and root replacement
before cached owner publication catches up. The final focused review found no
additional important consistency issue.

## Simulator and package evidence

The official GSP 1.5.1/GSPC 0.6.1 build has a separate `echoy_chain_desk` target
with three real group/band/PCM allocations, clocks offset by 0/200/400 ms and
simulated external physical/radio adapters. `tools/check_three_chain.py` drives
three official native UI hosts through actual C pointer callbacks. Normal and
reduced-motion evidence is retained separately; screenshots remain outside any
new device UI. The ordinary shared-band desk remains a reference target.

Both independent modes pass seven groups each over 2,640,240 actual PCM frames,
with zero late blocks/clipping and exact paused silence:
[normal](native-normal/checks.json), [reduced](native-reduced/checks.json).
The [ordinary native reference desk](native-reference/checks.json) passes all
13 existing groups, including language/reduced-motion propagation, original
musical flows and PCM output. Each runtime snapshots/hash-checks its binary,
bundle and generated API before launching, and checks natural cleanup.

The build uses pinned SDK `7b9cc1a` and the accepted office-voicing defaults.
Inspect the actual `output/device-three-chain-build` outputs: the low-noise
driver's summary artifact path points to an older default build and is not the
delivery artifact. The bundle targets `ota_0` (13,565,952 bytes); the retained
factory partition is intentionally smaller than the application, producing the
known size warning. It is excluded from the application destination. Package
inspection must verify the partition table, application and UI data only,
preserving Recovery/bootloader and the accepted layout hash.

## Delivery boundary

All three active devices ran accepted package `2b984e511b98` before this rollout.
Its immutable rollback bundle/tag and sound baseline `0953c52` remain intact.
The original two retain standing Echoy package authorization. On 2026-10-15 the
user explicitly approved exact new `7c6e37e7be4d` on replacement f45936 and freshly
confirmed all three separated at least 15 cm on a nonmetal surface. This completed
third-device installation does not grant blanket future writes. No old f40cd8
diagnostic or permission transferred to this replacement.

Official `mosaico.py iris test enter-recovery` followed by one `iris system-update`
per device succeeded with HEALTHY/validated and `commit_response` evidence.
Before/after bootloader-region and target partition hashes match; component and
actual ELF identities match the exact candidate. Read-only `iris device-status`,
`iris logs --snapshot` and `iris screenshot` verify each fresh boot, separated
calibration and actual Record-only home. All app crash, clipping, partial-write,
write-error and deadline-miss counters remain zero at 189.5/119.1/48.3 seconds.
Startup is silent; no microphone, reference tone or joined sound was exercised.

[Per-device delivery results](device-results.json) retain operation IDs, Boot IDs,
current-boot traces, real screenshots and retained component proofs. The original
two Recovery 0.1.2 instances each logged the known startup interrupt-watchdog
reset (reset reason 5) before a stable ready boot; crash metadata is saved, with
no valid Core Dump. Their new application boots have zero crashes. Replacement
Recovery 0.1.4 was ready with zero crashes. This does not fix the retained
Recovery issue or replace its firmware.

PC checks do not establish real RF, speaker quality, sensor behavior, acoustic
phase precision, sustained memory/load performance or physical three-board
acceptance. The [manual three-board checklist](physical-checklist.md) remains
outstanding after successful delivery/startup verification.
