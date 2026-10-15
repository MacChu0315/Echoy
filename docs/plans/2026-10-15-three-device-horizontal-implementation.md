# Three-device horizontal implementation plan

> Execute inline with the executing-plans and test-driven-development guides.

**Goal:** Three independent musical owners in a same-orientation horizontal chain.
**Architecture:** Portable per-peer transport and bounded group coordinator; one
physical worker and one musical owner on each device. Root/participant controls
use public routed unicast; phrase preparation follows actual left/right seams.
**Tech stack:** Portable C11, existing band/PCM, pinned GSP and ESP32-S31 SDK.
**Spec:** `2026-10-15-three-device-horizontal-design.md` (approved).

## Constraints

Preserve accepted tag `echoy-dual-accepted-20261014`, immutable package
`2b984e511b98`, sound `0953c52`, UI, recording, explicit per-device Play/Pause,
physical release and bounded retry/queue semantics. No SDK/BSP changes or new
production dependencies. Continue the existing feature branch in place; preserve
unrelated `.vscode/`. Do not update hardware before local/native gates.

## Review focus

Middle-node dual ownership; stale group/session messages after release; paused
members and first-start acknowledgements; unchanged banks at fixed boundaries;
readiness invalidation, deferred recording and bounded recovery after loss.

## Tasks

- [x] 1. Add a real owner regression for immediate Play before a fixed commit,
  including an unchanged root; align all continuously playing voices and group
  origin at the fixed boundary. Keep PCM before the boundary unchanged. Run
  `sh tests/cooperation/run_transactions.sh`, deferred, linked-device and band.
- [ ] 2. Extract portable wire/coverage/clock/slot primitives into
  `projects/echoy/main/echoy_link_protocol.c/.h`. Test malformed, fragmented,
  duplicated/reordered and stale payloads, per-edge clock mapping and reserved
  control capacity; preserve existing transport regression coverage.
- [ ] 3. Implement `echoy_group.c/.h`: maximum three physical members, root and
  generation fences, A→B→C complete candidates, individual readiness/ARM/COMMIT/
  activation, fixed timestamp and start acknowledgements. Run a host network of
  independent actual band/PCM owners with all six orders, pair/single, joins,
  releases, recording/reply, edits, restart, stale packets, loss and backpressure.
- [ ] 4. Integrate the group with `board_link.c/.h` and existing owner callbacks.
  Both horizontal contacts remain separately observed; network/sensor I/O stays
  on the worker. Validate physical/radio separation, allocation and stop cleanup.
- [ ] 5. Add an independent-owner native GSP harness under `projects/echoy/pc/`
  using the same group/band/PCM code. Verify actual C pointer callbacks and
  audible/silent PCM, including normal/reduced motion. Run existing regression
  suites, sanitizers, frozen hashes and pinned SDK build. Inspect exact package,
  configuration, UI, retained layout, memory and sound-source preservation.
- [ ] 6. Perform final independent review, fix substantive findings with failing
  regressions, save verified local commits/evidence and update current state.
  Prepare concrete device rollout only within applicable authorization; the
  replacement third device needs new authorization for the new package.

Each task records commands and observed results in the local execution ledger.
A failing check is investigated before proceeding to a release gate. Tasks are
not complete until their stated verification has actually run and passed.
