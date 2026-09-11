# Task 7 report — physical contact, protocol and measured clock

Base: `27c9491`; branch retained: `feat/echoy-mosaico`.

## Delivered

- Explicit little-endian 40-byte header / 60-byte body / 100-byte GAME_EVENT codec,
  all nine message kinds, canonical complete state encoding and CRC-32 digest.
- Static two-transaction reassembly per source, at most 16 fragments / 960 bytes,
  timeout, exact source/outer-boot binding, strict sequence/fragment order and
  completed-transaction rejection. No JSON, phrase audio or struct-image wire data.
- Four software-timestamp clock exchanges; low-RTT selection, at least eight
  measurements plus a 20 s span, linear drift fit, residual uncertainty,
  timestamp/RTT rejection and no demo readiness timer.
- Portable three-node PREPARE/READY/COMMIT/ACK state negotiation with future bars,
  COMMIT/READY retransmission, revision/digest matching, late-commit refusal,
  heartbeat-specific 750 ms loss and an additional 1.8 s hold/expiry.
- Complete committed-state installation API and `wire_managed` core sessions.
  Local transport, live activity, volume and faults cannot be overwritten by a
  peer; host tick/idle commit/contact timers cannot overwrite agreed parameters,
  availability or graph. Existing state oracle behavior is unchanged by default.
- Real `mosaico_network_start()` single ownership, real stable UID/boot getters,
  GAME_EVENT transmission independent of physical APP_DATA neighbor gating,
  32-entry nonblocking copied callback queue and overflow diagnostics.
- Actual BMM150 read → BSP classifier → presence tracker path; invalid/saturated/
  calibration failure remains uncertain. Actual local/peer edges, rotation and
  UID validation, ambiguous initial dual-edge rejection, bounded uncertain hold.
- `docs/echoy/protocol.md` documents fields, body lengths, states, ownership,
  software timestamp layer, trust boundary, rejection rules and failure limits.

## Tests and evidence

TDD observations:

1. Initial `wire_unit` failed at the missing encoder; `clock_unit` failed at the
   missing observation implementation. Both passed after implementation.
2. Initial `session_unit` failed at missing solo-state initialization, then passed
   after the portable three-node protocol implementation.
3. The new complete-state test exposed committed activity being overwritten by
   host bar/idle logic. `wire_managed` fixes pass that test and existing oracles.
4. Heartbeat-drop testing failed while TIME/HELLO packets wrongly masked missing
   heartbeats; loss accounting now advances only on actual HEARTBEAT messages.
5. Contact uncertainty expiry failed before bounded omission was implemented.

Final commands/results:

- `cmake -S pc -B build-host` and `cmake --build build-host -j8`: PASS.
- `ctest --test-dir build-host -R 'wire|clock|session|state' --output-on-failure`:
  **7/7 PASS** (wire_unit, clock_unit, session_unit, session_contact, wire_radio,
  plus state_unit/state_oracle).
- `ctest --test-dir build-host --output-on-failure`: **21/21 PASS**.
- Sanitizer configuration:
  `cmake -S pc -B /tmp/echoy-task7-asan -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie'
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined -no-pie'`.
  Build PASS. Then
  `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest
  --test-dir /tmp/echoy-task7-asan --output-on-failure`: **21/21 PASS**;
  no AddressSanitizer or undefined-behavior diagnostics.
- Initial leak-enabled run was blocked by LeakSanitizer's explicit
  `does not work under ptrace` runtime error in this sandbox. Leak checking is
  therefore **not verified**. AddressSanitizer and UBSan remain enabled in the
  successful run. The fresh host build also emits existing LVGL shader raw-string
  and `strnlen` warnings; no dependency source was edited to suppress them.
- Activated
  `/home/zhuzhengming/esp/esp-idf/esp-idf-echoy-v6.2-dev/export.sh`, then
  `idf.py build`: **PASS**, latest image **3,154,848 bytes**. Actual radio/contact,
  Mosaico network/peer/classifier and core objects compiled successfully.
  Final incremental log has no compiler warning/error. Initial registry lookup
  failed under restricted network; an approved read-only metadata build resolved
  the existing dependencies. Existing remote registry versions were not changed;
  lockfile gained local BSP interaction/peer dependencies.
- `git diff --check`: PASS.

Three-instance fake-radio coverage includes queued 500 us one-way software delay,
COMMIT drop to one member, duplicate frames, reversed delivery order, partition,
reference power-off before the boundary, changed reference boot, HELLO-only
strangers and heartbeat loss despite continuing TIME/HELLO traffic. A missing
final COMMIT leaves the old committed state; version divergence enters degraded
syncing instead of claiming atomic partition-safe commit.

## Integration requirements / concerns

Task 8 must connect these owner APIs to the lifecycle/control/UI/audio tasks. This
commit adds the adapters and portable implementation but does not start radio or
contact sampling from `app_main()`.

1. The control owner must retain real UID/boot bindings. `authorize()` requires
   physical/group trust evidence; HELLO alone never grants membership. In the
   current portable implementation **all proposed members must already be
   explicitly authorized**. For an endpoint that has never directly identified
   another group's remote member, Task 8 needs a verified group-member handoff,
   or prior individual physical identification. The three-node host simulation
   installs trusted bindings explicitly; it does not test a completed autonomous
   physical 2+1 group admission flow.
2. The owner must assemble the full directed graph (the contact adapter reports
   only local edges), gather member parameter requests and real readiness, supply
   the next complete proposal/revision, and map committed session tuples to a
   noncolliding local core session ID.
3. The owner must dispatch heartbeat loss times as `ECHOY_LINK` inputs, handle
   core continuous-time reference election/hold splits, and explicitly rebase or
   recreate protocol state for a surviving group after expired members leave.
   The portable proposal validator never silently removes a committed member.
4. Wire-managed core sessions freeze committed state until the next protocol
   commit. Pending local parameters and role readiness therefore require the
   Task 8 coordinator/request path; live volume/activity and local play/pause
   remain immediate as specified.
5. PREPARE fragment loss/reordering requires a new owner-driven transaction with
   a new future boundary. COMMIT/READY are retransmitted; incomplete PREPARE is
   deliberately not repaired by accepting reordered/duplicate fragments.
6. Two-/three-device magnetic identification, calibration, GAME_EVENT retention
   after release and acoustic onset skew have not been measured here. Saturated
   BSP classifier capture examples are intentionally uncertain, so actual
   reliable unsaturated contact behavior must be verified on hardware.
7. Atomic commit across arbitrary partitions is not promised. A reference that
   dies less than one detection budget before the boundary cannot be known
   instantly. Missing commits preserve the previous state and trigger degraded
   renegotiation on disagreement.

No sibling repository was edited, no remote state was written, and no hardware
was flashed. The task report is tracked alongside the prior task reports in `.superpowers`;
the durable protocol contract is in `docs/echoy/protocol.md`.

## Fix round 1 — revalidate waiting clocks and anchor integer rate

Review requests 1–3 were reproduced before changing production code.

- RED `session_clock_freshness`: after READY/COMMIT, dropping TIME_RESP while
  continuing HEARTBEAT did not cancel the pending transaction.
- RED `session_clock_unstable`: new offset-unstable samples likewise did not
  invalidate the earlier READY promise.
- RED `session_clock_long`: three independent software clocks with startup
  offsets 1,000,000 / 2,500,000 / 250,000 us and rates 0 / +100.4 / -37.6 ppm,
  exchanging timestamps for 12 simulated hours, exposed **16,913.882 us** of
  installed boundary error at node 1. Mapping the old epoch at full precision
  and then rounding only the stored rate caused that accumulated error.

Changes:

- Pending synchronization now checks every proposed member's current boot,
  authorization, link state, stable clock fit, <=2 ms uncertainty and <=750 ms
  measurement age on owner ticks and incoming messages. TIME_RESP checks again
  immediately after fitting its new sample. The final installation path performs
  the same check. The latest mapping must remain within 2 ms of the agreed apply
  time. Before the first COMMIT, at least 42 ms of lead must remain (40 ms for the
  prepared scheduler plus the uncertainty budget).
- A failed check sends matching ABORT to all proposal members, retains old
  committed state and enters degraded/syncing. The coordinator cannot keep
  retransmitting COMMIT for an aborted uninstalled revision. Fresh measurements
  alone do not clear the failure or revive the old transaction; both new failure
  tests verify a subsequent fresh negotiation succeeds after clock recovery.
- `map_state()` maps the agreed apply time, computes the final integer rate, then
  recalculates epoch using **that stored rate and apply bar**. Rounding epoch down
  introduces less than 1 us of forward phase quantization at the agreed boundary,
  independent of the age of the reference epoch.
- The three-instance fake radio now uses shared physical simulation time only
  for packet scheduling. Every node receives its own independently offset/rated
  local timestamps. Commit callbacks assert the session key and independently
  calculated physical apply time and retain epoch/rate/apply fields for phase
  checks. Long-run testing calls actual `echoy_core_install()` and
  `echoy_phase_steps()` and checks the installed boundary and monotonic phase.
- The clock-drop/unstable injection targets just **one** member; the other
  members must abort through the protocol, rather than independently seeing the
  same injected fault. Heartbeats continue, confirming this is clock-readiness
  handling rather than ordinary heartbeat loss.

Final measured host results after the 12-hour timestamp-exchange run:

| Node | Stored boundary error | Error against independent physical clock |
| --- | --- | --- |
| 0 | 0.000 us | 0.000 us |
| 1 | 0.882 us | 0.400 us |
| 2 | 0.611 us | 0.400 us |

Validation on the final code:

- `cmake -S pc -B build-host`; `cmake --build build-host -j8`: PASS.
- `ctest --test-dir build-host -R 'wire|clock|session|state' --output-on-failure`:
  **10/10 PASS**.
- `ctest --test-dir build-host --output-on-failure`: **24/24 PASS**, 20.85 s.
- Reconfigured/rebuilt `/tmp/echoy-task7-asan` with the existing
  `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie` flags, then
  `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest
  --test-dir /tmp/echoy-task7-asan --output-on-failure`: **24/24 PASS**, 42.91 s;
  no sanitizer diagnostics. LeakSanitizer remains subject to the previously
  documented ptrace limitation; neither ASan nor UBSan was disabled.
- Activated the existing ESP-IDF export and ran `idf.py build`: PASS;
  **3,154,848 bytes**. The changed protocol object compiled; the Task 8 firmware
  runtime still has not been connected, so the application image size is unchanged.
  Final incremental log has no compiler warnings/errors.
- `git diff --check`: PASS.

These are software simulation results, not hardware onset measurements. Integer
rate still has post-installation quantization error; subsequent negotiated
anchors are required to bound that error during continued performance. The
reviewed fix prevents *prior uptime* from magnifying epoch/rate inconsistency at
installation. The existing 2+1 trusted-member handoff and actual magnetic/acoustic
measurements remain with Tasks 8/10 as requested.
