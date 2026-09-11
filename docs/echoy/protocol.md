# Echoy GAME_EVENT protocol, version 1

This document describes the implementation in `echoy_wire.h`, `echoy_clock.h`,
`echoy_protocol.h` and `echoy_platform.h`. It is a three-device, single-hop
protocol. Every member must remain in radio range of every other member. It does
not stream audio or JSON phrases. All nodes compute the entire ordered chain
with algorithm version 1, using their own local output volume.

## BSP boundary and ownership

`echoy_radio_start()` calls `mosaico_network_start()` once. Calling it again on
that same object succeeds without reinitializing Wi-Fi; another owner fails.
`mosaico_network` owns Wi-Fi/ESP-NOW, its handshake and physical neighbor table.
Echoy sends `MOSAICO_PEER_MSG_GAME_EVENT` with
`mosaico_peer_link_send_broadcast_to()`, with BSP application session/edges zero.
It never uses the 64-byte `mosaico_network` APP_DATA path for performance traffic.
GAME_EVENT has a 100-byte payload and remains available after physical release.
No second call to `mosaico_peer_link_init()` is made.

The raw callback checks the outer type/size and copies the complete BSP message
and `esp_timer_get_time()` into a 32-entry FreeRTOS queue with zero wait. It does
not parse, calculate, dispatch state, send responses or touch UI/audio. Neighbor
callbacks similarly enqueue copies. Overflow increments `radio.dropped` with an
atomic operation. Read that counter atomically if another task reads it. The
single control owner polls the queue, filters outer target/source, and passes
`source_id`, `source_boot_id`, payload, enqueue timestamp and current send time to
`echoy_protocol_receive()`. Timestamp units are signed 64-bit microseconds.

Stable wireless UID and boot come from `mosaico_peer_link_get_device_id()` and
`mosaico_peer_link_get_boot_id()`. The latter is a 32-bit boot token, not an
ordered counter. ESP-Iris operational identities are separate; do not substitute
one for the other. No fixed A/B/C identity is used by the adapter.

## Discovery and admission

HELLO is discovery only, even if a packet advertises an existing group. It cannot
start transport, admit a peer, replace a boot binding or install state.
`echoy_protocol_authorize(uid, boot, now)` is a control-owner operation requiring
identified, reliable contact evidence. The owner obtains the UID from the real
BSP neighbor event and binds the boot from a parsed HELLO with the **same UID**.
A neighbor event itself has no boot field. The owner must retain the confirmed
UID/boot mapping for all proposed members, including members learned through an
already verified group. Do not call authorize for arbitrary nearby HELLOs.

The portable protocol deliberately does not infer transitive authorization from
a received proposal: every member UID/boot in PREPARE must already have a trusted
binding. A three-node integration therefore needs a verified group-member handoff
at the control layer, or prior individual physical identification. Merely seeing
two groups near each other does nothing. The host fault simulation explicitly
installs the trusted bindings; it does not claim to validate that physical handoff.

A different boot from a bound UID is rejected and degrades pending synchronization.
It does not overwrite that boot. Explicit fresh physical authorization clears the
old peer clock/reassembly/sequence state and requires new clock measurements.
An expired peer must likewise be explicitly reauthorized. Neither operation
starts a paused device. The BSP radio is not cryptographically authenticated by
this application protocol: source checks and CRC prevent misassociation and
accidental corruption, not hostile on-air identity forgery.

## Magnetic evidence

On the owner, sample both populated BMM150s every 20 ms using
`bsp_magnetometer_read(BSP_MAGNETOMETER_0/1)` (right/left), then run the actual
`MOSAICO_MAG_CALIBRATION_S31_V1` classifier and BSP presence tracker. Read errors,
incomplete calibration, calibration failure, invalid classification and
saturation all mean **uncertain**. They cannot create a contact claim. Known
relations are reported uncertain for at most 1.8 s, then omitted. The hardware
profile includes saturated capture examples; those examples do not override this
safety rule. Reliable unsaturated contact performance needs bench measurement.

Only right→left and left→right neighbors with zero relative rotation are accepted.
The UID and actual `local_edge`/`peer_edge` come from BSP, never RSSI or magnetic
strength. Two initially unidentified left/right contacts at once suppress both
claims and remain ambiguous; separate the devices and identify one new edge at a
time. Top/bottom contacts, conflicting orientation, one UID on both sides and
occupied-edge replacement are rejected. The complete state decoder additionally
rejects duplicate members, duplicate/inverted edges, branching and cycles. At
most three members and two directed edges are supported.

`echoy_contact_report()` returns only the local part of the contact graph. The
control owner combines this with other confirmed members' directed edges before
submitting a complete `ECHOY_CONTACTS` report or a new state proposal. Do not
replace the complete graph with a single device's partial report. Physical
release removes a relationship at a committed bar but does not revoke an
established radio member or stop heartbeats.

## Packet header

Every integer is explicitly little-endian. C structure layout/padding is never
sent. Every packet is exactly `40 + body_length` bytes, at most 100 bytes.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 2 | magic `0x4543` (bytes `43 45`) |
| 2 | 1 | version `1` |
| 3 | 1 | kind, `1..9` below |
| 4 | 4 | sender boot, must equal outer `source_boot_id` |
| 8 | 4 | nonzero sender-wide increasing sequence |
| 12 | 8 | session creator UID |
| 20 | 4 | session creator boot |
| 24 | 4 | nonzero session nonce |
| 28 | 4 | nonzero transaction ID for kinds 5..9; otherwise zero |
| 32 | 4 | nonzero apply bar for kinds 5..9; otherwise zero |
| 36 | 1 | fragment index, zero based |
| 37 | 1 | fragment count, `1..16` |
| 38 | 1 | body length, `1..60` |
| 39 | 1 | flags, zero in version 1 |

Session identity is the complete `(creator_uid, creator_boot, nonce)` tuple.
The creator is not necessarily the currently surviving clock reference. Sequence
and transaction counters cannot wrap within a boot; reaching the limit requires
a fresh boot/session. Exact duplicates and out-of-order sequences are rejected.
Idempotent COMMIT/ACK **retransmissions use new sequences**, retaining the same
session/transaction/apply bar/revision/digest. Ordinary messages have fragment
index 0/count 1. PREPARE is the only fragmented message; every nonfinal fragment
has exactly 60 body bytes.

## Message bodies and accepted states

Offsets in this table are relative to the body. All fields must fit exactly;
truncation, padding, unknown version/kind, reserved header flags and invalid
fragment metadata are rejected before processing.

| Kind | Bytes | Body | Accepted use |
| --- | --- | --- | --- |
| 1 HELLO | 16 | member count u8@0, reserved zero@1, reference UID u64@2, committed revision u32@10, algorithm u16@14 | Discovery in any state; only authorized matching boots update election metadata |
| 2 TIME_REQ | 8 | t1 u64@0, bounded by INT64_MAX | Authorized current boot, independent of physical edge |
| 3 TIME_RESP | 24 | t1/t2/t3 u64@0/8/16 | Authorized current boot; t1 must match latest outstanding request |
| 4 HEARTBEAT | 8 | committed revision u32@0, degraded flag u32@4 (0 or 1) | Authorized current boot; matching members expose revision/session divergence |
| 5 PREPARE | 1..60/fragment | slices of canonical state below | Elected authorized reference, no active proposal; full state/clock/admission/preparation validation before READY |
| 6 READY | 8 | state revision u32@0, state digest u32@4 | Coordinator's current proposal, included peer, strictly before apply boundary |
| 7 COMMIT | 8 | same revision/digest | Prepared follower, exact reference/session/transaction/bar/digest, before boundary; committed duplicate is ACKed |
| 8 ACK | 8 | same revision/digest | Coordinator's matching scheduled or committed proposal, included peer |
| 9 ABORT | 8 | same revision/digest | Included peer's matching active proposal; enter degraded/syncing, preserve old committed state |

The transaction digest is the stored canonical-state CRC value, not a CRC of the
state including its CRC (which would yield a constant residue). ABORT is accepted
for explicit owner/application cancellation; automatic local failure uses the
same degraded state and the peers discover failure via timeout/revision checks.

## Canonical state and bounded reassembly

The serialized state is `44 + 24 * member_count + 2 * edge_count` bytes (68..120
bytes currently). Version 1 reserves a bounded framing capacity of 960 bytes /
16 fragments; unused capacity is not an alternative JSON or phrase channel.

| Offset | Bytes | State field |
| --- | --- | --- |
| 0 | 2 | algorithm version, 1 |
| 2 | 1 | member count, 1..3 |
| 3 | 1 | directed edge count, 0..2 |
| 4 | 4 | nonzero state revision |
| 8 | 8 | epoch in reference software-clock microseconds |
| 16 | 4 | signed two's-complement rate correction, -500..500 ppm |
| 20 | 8 | reference member UID |
| 28 | 8 | transport request owner UID, or zero for observations only |
| 36 | 4 | reserved zero |
| 40 | 24 each | member records |
| after members | 2 each | left index u8, right index u8 |
| final 4 | 4 | CRC-32/ISO-HDLC of all preceding state bytes |

Each member record contains UID u64@0, boot u32@8, role u8@12 (0..3), phrase
u8@13 (1..4), committed activity u16@14 (0..1000, quantized in 0.001 units),
motif available u8@16 (0/1), transport observation u8@17 (paused/waiting/playing
0/1/2), and six reserved zero bytes@18. There is no volume field. Epoch/rate,
member identities, parameters, availability, observations and graph all share the
same revision and CRC. Algorithm mismatch or inconsistent topology rejects the
entire state. Every proposal must preserve existing members; heartbeat loss and
hold expiration are separate membership operations.

Each of at most two other devices has exactly two statically allocated 960-byte
reassembly slots. A third unfinished transaction returns `ECHOY_WIRE_FULL` and
increments the protocol diagnostic. Slots expire after 2 s. Fragment zero must
arrive first, then each following index exactly once with consistent count,
session and apply bar. Reordered/dropped fragments do not trigger partial state
installation. Old completed transaction IDs, wrong source/boot, duplicate
sequences, malformed final size, unsupported algorithm and CRC mismatch are
rejected. A failed proposal is restarted by the owner with a **new** transaction
and future boundary; the parser does not silently accept reordered fragments.

## Measured clock mapping

A sends t1; B records t2 when its raw callback enqueues the packet and t3 immediately
before the control task encodes/sends its response; A records t4 in its raw
callback. Thus these are software enqueue/send timestamps, **not hardware PHY
timestamps**. Scheduling, queueing, ESP-NOW and radio asymmetry remain in the
uncertainty budget.

```
offset = ((t2 - t1) + (t3 - t4)) / 2
RTT = (t4 - t1) - (t3 - t2)
remote(local) = local + offset_at_anchor + rate * (local - anchor)
```

Reject negative/backward timestamps, nonincreasing exchanges, B transmit before
receive, negative RTT and RTT above 100 ms. Retain at most 96 measurements over
20 s, with one sample preceding the window to establish span. Fit offset against
local midpoint time using measurements within 1 ms of the minimum RTT. Require
at least eight selected measurements **and a full 20-second span**. Rate is the
linear-fit slope, limited to ±500 ppm. Estimated uncertainty is minimum RTT/2
plus maximum selected residual; readiness requires at most 2 ms. Mapping also
requires a valid exchange in the last 750 ms at proposal validation. Epoch
mapping may legitimately be negative in the local time domain. Rate conversion
combines reference timeline rate with measured local/reference drift.

These thresholds and the 250 ms sampling interval are diagnostic defaults awaiting
hardware validation. They are not an acoustic onset guarantee. Unstable offset,
large asymmetry, insufficient span or stale measurement leaves synchronization
pending. There is no 1.05 s automatic readiness timer. The host clock fixture
checks a known +100 ppm drift and rejects low-RTT but unstable offsets.

## Negotiation, loss and recovery

Select the session with most members; ties use the lowest reference UID. Only its
reference begins a proposal. The owner supplies a revision greater than every
participating prior revision, the complete member bindings and graph, and real
preparation readiness via the optional prepare callback. A null callback means
there are no additional resources to prepare; it never bypasses clock checks.

The coordinator chooses a reference bar at least two bars plus 40 ms in the
future. A follower requires at least two full local bars of lead time on complete
PREPARE receipt. All included peers must READY before the coordinator starts
COMMIT. Followers repeat READY at 250 ms until COMMIT arrives. The coordinator
repeats COMMIT at 250 ms for missing ACKs until 750 ms after the boundary.
Followers ACK each matching COMMIT, including retransmissions after their own
commit. At the agreed boundary only a node with a complete COMMIT installs the
state. Late PREPARE, READY and first COMMIT cannot catch up by committing elapsed
bars. A sparse owner tick installs the scheduled current state once and gives the
scheduled boundary to the callback; the downstream audio scheduler must discard
elapsed events, as specified by the audio output API.

A callback receives the complete state, session tuple, mapped local epoch/rate and
scheduled apply time. `echoy_core_install()` validates UID/boot slots and installs
that complete chain on the control owner. The resulting `wire_managed` core
session computes motifs each bar but freezes committed parameters/availability/
relationships until another wire commit. It cannot silently overwrite them from
live activity, idle parameter dispatches or the host contact timer. Local volume,
live activity, faults and transport remain local. Remote transport is an
observation only; even a state claiming that the local node is playing cannot
start that node. The control owner must present local pending parameter/request
state to the coordinator for later transactions; the transport_request_uid field
never executes a remote play command.

HEARTBEAT is emitted every 250 ms. Three **heartbeat** misses (750 ms, even when
TIME/HELLO packets still arrive) mark a peer lost. The core owner should dispatch
`ECHOY_LINK=0` using the recorded loss time. The additional hold is 1.8 s from
that application loss time; an expired peer requires reauthorization. This is
separate from the BSP physical-session stale timeout of 2.5 s. During a reference
loss the existing core elects the lowest surviving UID while preserving its
local epoch/rate; the protocol is degraded and the owner must negotiate a new
revision using that surviving reference and the continuous mapped timeline.

Final COMMIT/ACK exchange is not atomic consensus under arbitrary partition. A
member that misses every final COMMIT keeps its old state. A reference lost at
least the heartbeat timeout before the boundary prevents follower installation;
a reference dying closer than the detection budget cannot be known instantly.
Heartbeats reveal revision/session disagreement, and nodes mark degraded/syncing
for owner-visible error/re-negotiation. They do not invent successful readiness,
replay old bars, automatically unpause a device or claim that all partitioned
nodes committed atomically.

## Verification and remaining hardware work

`wire_unit` covers framing, endian bytes, all kinds, truncation, reserved flags,
CRC/topology, source/boot/sequence/transaction checks, ordered bounded reassembly,
and complete core installation without local transport/volume changes.
`clock_unit` covers known drift, stability window and malformed exchanges.
`session_unit` runs three real portable protocol instances over a queued fake
radio with delay, drop, reorder, duplication, partition and reboot. It tests
final COMMIT loss, reference loss before the boundary, revision degradation,
unauthorized HELLO isolation and heartbeat-specific loss. `session_contact`
runs the actual sibling BSP classifier with fake sensor reads and checks adapter
uncertainty, bounded retention and direction/UID rejection. `wire_radio` checks
copy ownership, bounded callback queue, targeting and one-time network start.

Required bench follow-up: two/three identified physical devices; unsaturated
classifier behavior and calibration; sequential new-edge identification; verified
group-member handoff; real receive/RTT/residual/rate diagnostics; physical release
with continuing GAME_EVENT traffic; reference power-off/restart and partitions;
and acoustic onset skew at the DAC/speaker output. Host clocks and successful
IDF compilation do not substitute for those measurements. Task 8 connects these
owner APIs to lifecycle, UI/audio and actual product control flow.
