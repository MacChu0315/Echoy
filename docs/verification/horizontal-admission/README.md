# Application-owned horizontal admission and bounded transport

Approved design: ../../plans/2026-10-15-horizontal-admission-repair.md.
The change replaces generic automatic neighbor/session selection with portable
`echoy_radio` on public `mosaico_peer_link` APIs. No SDK/BSP source is changed.
The same scene network ID is retained so legacy claims remain observable; only
explicit ECHN/version1 peers can complete the new admission handshake. Inner
musical group v3 payloads and phrase format remain compatible in meaning, but
old network packages cannot cooperate with this adapter.

## Behavior

- Every observed participant advertises complete ready/contact state, ID, boot
  and contact generation. A challenge/reply validates a boot before replacing
  its state. Accepted sequence/generation watermarks survive RX recovery.
- All fresh participants must agree on the same unique R+L or R+LR+L scene,
  including idle mask0 members. Reciprocal proposals/readiness precede admission.
  Same-side pairing and endpoint shortcuts are excluded from this complete view.
- A local confirmed release immediately removes its seam; fresh peer release
  also ends that seam. Radio silence/RX overflow retains physical membership
  and reports recovery. Stale known participants are not silently forgotten.
- New graphs bind all tuple generations. Old graph/boot/sequence packets cannot
  operate the current owner. Missing previously observed participants block new
  admission until fresh evidence returns; roster state is RAM, not calibration.
  Binary local masks cannot identify a completely unobserved RF-hidden middle;
  this is the approved observability boundary, not claimed hardware certainty.
- Raw sends are spaced at least10ms, or50ms after a submit error. Six originated
  operations, four relay slots and four ACK slots are bounded. Reliable operations
  have at most five attempts/2s lifetime; old group30s preparation bounds remain.
  A middle forwards one hop; route direction/graph identity reject loops/stale
  routes. A256-position receive window deduplicates delivered operations. An
  unacknowledged operation fails rather than claiming delivery.
- Clock PING/PONG timestamps are set immediately before the first accepted raw
  submission. Accepted retransmissions and forwarding keep identical bytes.
  This removes queue waiting from clock samples without relaxing error checks.
- Sound, UI, office voicing, Flash horizontal profile600, sensor filtering and
  debounce, paused/playing permissions and Recovery are retained.

## Local verification

- `sh tests/horizontal_radio/run.sh`: all six same-scene broadcast permutations,
  direct/two-hop delivery, fast ABC→BCA, pairs/isolated third, same-side rejection,
  stale known middle/legacy, loss/recovery, bounded retries/backpressure, boot
  replay, overflow, stale-position replay after recovery, first-send timestamp
  retry/forwarding immutability. Initial missing implementation and both found
  regressions have retained red/green evidence.
- `sh tests/group/run_chain_host.sh`: actual three independent group/band/PCM
  owners use this real admission/transport, not ideal neighbor routing. All six
  orders, immediate root-to-tail reorder, detach/paused retention and known
  70/140-second clock-offset accuracy pass.
- Final25 host groups pass (`final-host-results.json`), including51 runtime
  cases, existing reference arrangement/music/audio and legacy rollback tests.
  Initial Arrange LSan was blocked by ptrace; the complete original check passed
  outside ptrace. Sanitizer was not disabled.
- Radio, actual chain and board adapter pass ASan/UBSan/LSan. Sensor/contact
  regression retains all25 pinned fixtures and the recorded saturated seam.
- Independent read-only review found and closed one stale-replay issue, and
  separately reviewed the first-wire clock stamping correction (`review.md`).
- SDK build run20261015-174245-system-update-bundle-846077 succeeds; only the
  known retained factory-size warning remains (package targets OTA). Final ELF
  uses the public peer-link layer and contains no mosaico_network_start symbol.
  Shared44-byte thresholds are still in `.flash.rodata`.
- Exact source, configuration and component identities are in normal-candidate.json
  and source-hashes.json. Accepted sound/UI source and frozen v4 hashes match;
  UI data/retained partition components are byte-identical to the accepted bundle.

## Native simulator and hardware status

The first native invocation lacked Release manifest configuration; corrected
without a source change. The first actual Release run exposed13.387ms clock
error at the existing10ms limit. That failed evidence is retained locally and
motivated the tested first-wire timestamp fix; do not count it as acceptance.
Final normal and reduced-motion runs each pass all seven actual native checks.
Both have zero PCM late blocks/clipped samples and exactly silent paused windows;
original10ms clock comparison passes. Evidence is in native-normal/native-reduced.
Package9d00b69f4fce is verified locally; hardware delivery and real fast-reorder
acceptance remain pending. Earlier6a85b7b9ce13 was never installed.

## Initial hardware result (9d00b69f4fce)

Installed/HEALTHY on all three; identity, retained components, fixed sampling and
startup audio checks pass. A explicitly admits C on its LEFT; all three report
a fresh admitted three-participant scene. However stable transport is not
accepted: B/C show repeated ESP_ERR_ESPNOW_NO_MEM; C has1174 submit failures and
206 relay/ACK queue drops in the captured cumulative counters. A remained at
zero submit failures in its captured window. B/C/A owner states were1/5/2 with
empty banks, so do not count following or original fast-reorder acceptance.

First-wire spacing alone does not bound the driver's inflight work. The pinned
SDK documentation recommends waiting for the previous ESP-NOW send callback;
public peer-link does not register one. A follow-up adds application-owned TX
completion gating and separate MAC completion counters, retaining reliable
application ACKs. Its hardware effect is not yet verified.
