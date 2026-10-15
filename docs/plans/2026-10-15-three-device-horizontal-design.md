# Three-device horizontal cooperation design

Status: approved by the user on 2026-10-15, including first Play during a
locked update and common boundary alignment for unchanged instruments.
Implementation is in progress; three-device firmware has not been installed.

## Scope and retained baseline

Support at most three Mosaicos in the same scene, all upright and joined only
along complementary horizontal RIGHT/LEFT edges with relative rotation zero.
Support independent devices, any pair, and one A—B—C chain. A/B/C denote current
physical order, not fixed MACs. A tile keeps its own role, settings and local
Play permission when moved. No vertical, L-shaped, branching or rotated layout.

Current active hardware:
- `4553502d49524953010030eda0f452a0`
- `4553502d49524953010030eda0f40c6c`
- replacement `4553502d49524953010030eda0f45936`

The replaced f40cd8 unit is outside this rollout; its sensor diagnosis is not
applied to f45936. Preserve accepted implementation `cca129d`, local tag
`echoy-dual-accepted-20261014`, package `2b984e511b98`, sound baseline `0953c52`
and their immutable artifacts. Keep accepted office voicing enabled and all
microphone/reference-tone diagnostics disabled. Magnetic logging stays off in
normal builds. Frozen HTML, UI layout/resources and musical synthesis stay intact.

## Product behavior

1. Music flows left to right. B derives from A's complete phrase; C derives from
   B's complete phrase, retaining the existing structured accompaniment rules.
   Each device still chooses its own role/expression and volume.
2. Continuous playback always needs local Play. Any of A/B/C can start an
   otherwise paused group promptly; later entrants join its common bar. Receiving
   music, acknowledging a clock or committing an update cannot unpause a peer.
   Nearly simultaneous first presses converge to one deterministic shared origin.
3. A paused playable follower shows one centered Play. An empty/unready follower
   keeps Play disabled. Record is unavailable while actually following; existing
   deferred count-in/record/reply and Cancel/Pause behavior remain.
4. Physical separation removes the affected membership immediately, independent
   of the next musical commit or radio ACK. Keep each current complete phrase,
   playing/paused permission and audible phase. Remaining connected segments get
   their own clock ownership. Removing B leaves A and C independent; removing A
   leaves B—C as a valid group headed by B. Solo replay ignores historical clocks.
5. A radio-only interruption on a retained seam shows recovery and retains the
   physical relationship. Old sessions, transfers, starts and queued commands
   cannot recreate a released seam or expose an abandoned proposal.
6. Joining during local count-in, recording, preparation or one-shot reply is
   deferred until that operation finishes or is cancelled. A deferred edge does
   not indefinitely block an already-active component. A middle tile may finish
   its existing B—C activity before accepting a new upstream A.
7. Normally connected participants prepare all affected sound banks before a
   common future boundary. Old committed PCM continues during preparation. Failed
   preparation keeps committed sound. Edits after ARM queue for the next attempt;
   volume/Pause stay local and immediate. No unbounded retries.

### First Play near an already locked update

Keep the previously negotiated ARM/COMMIT timestamp fixed. A first Play still
starts the current committed phrase promptly from its beginning. At the locked
boundary, all participants adopt that boundary as their common bar origin,
including a participant whose notes themselves did not change. Consequently,
the first loop cycle can be shorter than a full bar in this overlapping case.
Ordinary idle first Play starts a full cycle; ordinary later entrants wait for
the shared bar. The user approved this explicit edge behavior on 2026-10-15. It preserves prompt first playback, recorded initial silence, local
permission and the immutable commit time without silently retiming a peer.

## Chosen approach and alternative

Recommended: retain per-device musical preparation and extend coordination along
confirmed physical edges. B receives from its left and supplies its own complete
candidate/current phrase to its right. The leftmost admitted node coordinates the component's future boundary from
each participant's readiness and authorization reports over public routed unicast.
This reuses existing owner jobs, codec, recording policy and calibrated banks.

Alternative: make the leftmost device compile all three final plans and send
individual results through B. This centralizes arrangement decisions but changes
more owner/import paths, moves remote edits through the root and makes root
replacement more invasive. It is not selected for this bounded three-node chain.

## Components and ownership

- Extract the existing per-peer wire/transfer/clock state from `board_link.c`
  into a portable application-owned `echoy_link_protocol.c/.h`. It accepts copied
  messages/delivery events and logical time through callbacks, without BSP,
  FreeRTOS, Iris, UI or audio I/O. Preserve existing codec/CRC/coverage fences.
- Add `echoy_group.c/.h` for the bounded three-node component: admitted edge
  membership, root identity/generation, participant readiness, common boundary
  and clock-start requests. No generic arbitrary-size graph framework.
- `board_link.c/.h` remains the single physical worker/network owner. It owns at
  most one physical LEFT and one physical RIGHT edge, plus a bounded roster of
  at most two other participants. It dispatches by source/session/group identity.
  Keep public BSP magnetic classification, contact gating and startup policy.
- Extend `echoy_coop.h` and `echoy_band.c/.h` copied contracts so a middle node can
  follow an upstream while holding its outgoing candidate for a downstream.
  Publish the local committed/candidate plan, not the upstream replica, to C.
  One local group command drives the band owner; two peer contexts must not
  independently mutate or double-authorize the same local immutable job.
- Firmware and native integration use the same portable protocol/group code and
  shared C band/scheduler. The existing single-band desktop is retained as a
  reference; it does not by itself validate three independent physical owners.

## Membership, transactions and transport

Maintain separate physical and admitted musical edges. Validate a maximum-three,
nonbranching directed component before admission; avoid inferring membership
from retained music or radio proximity. Per-edge connection generations fence
released links. A component identity includes root ID plus a fresh generation;
transactions bind that identity, a root attempt and each participant's revision.

Use an explicitly versioned private v3 coordination envelope. Retain canonical
plan bytes. Bind group identity in metadata/control and per-edge transfer
identity; fragments cannot cross sessions/groups even when their music CRC is
the same. The pinned `mosaico_network_send` already provides reliable routed unicast
through the confirmed connected component. Use it for root/participant controls;
A→C controls follow the physical A—B—C route. Phrase dependency remains A→B→C.
Do not reimplement routing, admit C from radio proximity, use raw ESP-NOW or
change SDK/BSP sources. Keep physical membership separate from radio-only mesh
availability; a missing radio route is recovery, not a physical release.

A prepares its proposal; B prepares its own derived bank and offers that complete
candidate to C. B and C individually report readiness to the root using routed
unicast; C cannot report ready until its actual B-derived bank is prepared. Only
when all required participants are ready does the root send ARM. Each local
owner acknowledges its frozen revision/time. COMMIT/authorization and actual
activation have distinct acknowledgements; the root does not infer C readiness
from B's transport delivery or local preparation. A local B/C edit withdraws its readiness before ARM or
queues after ARM. A root transaction has one unchanged 30-second overall ceiling;
child traffic/recovery does not reset it. Keep the existing one-second minimum
lead and 200 ms guard, selecting a later whole bar before ARM if needed.

Reserve one control lane and up to two data chunks per active destination; each
node has at most two other participants and therefore at most six outstanding
application operations within the pinned eight-slot transport. Retired
transfers retain their slots until delivery/timeout. Controls outrank data.
Backpressure or allocation failure keeps old PCM and bounded recovery. Existing
clock RTT rejection, sampling and frozen armed-time mapping remain.

Loss after final authorization can still reach only a subset of nodes. Detect
and report bounded recovery; do not claim perfect atomic delivery across an
arbitrary radio partition or treat a timeout as confirmed physical separation.

## Clock and lifecycle safety

Sample clocks on physical neighbor edges separately, retaining the existing
per-edge RTT bound; do not mistake a slower routed two-hop RTT for a one-edge
sample. Carry the root musical origin/mapping through B, then freeze each local
ARM timestamp.
Start requests include requesting node/session/serial and are acknowledged back
to the originator. Preserve copied pending local origins before either hop has a
valid clock estimate; old metadata cannot overwrite them. Aggregate playing
state prevents a later entrant from resetting a running group's clock.

The locked boundary also schedules the post-commit bar origin. A concurrent
first start cannot move that boundary; clock updates are ordered/fenced so an
old start ACK arriving after commit cannot restore the pre-commit origin.

Radio/RF initialization remains before audio activation. Sensor/network/encoding
work stays on the link worker, large buffers and cold group state in explicit
PSRAM. The PCM owner never waits for link/UI readers. Stop/flush failures retain
live contexts for retry; no callback may use freed per-peer/group storage.

## Verification gates and rollout

1. Preserve existing model/music/codec/transport/band/runtime and actual PCM
   regressions. Keep the accepted sound, waveform budgets, Recording 3/2/1 +
   four beats, brightness and frozen-reference hashes.
2. Add three independent owners through the real portable protocol, immutable
   preparation and PCM chains. Cover all six physical orders, singles/pairs,
   both attachment orders, A/B/C-first and simultaneous starts, repeated Pause,
   middle/endpoint detach, stale queued starts/transfers and new sessions.
3. Cover deferred operations at each applicable node, empty/first phrases,
   upstream and middle/last edits, readiness withdrawal, queued post-ARM edits,
   failures/backpressure, lost/delayed/duplicate controls, partitions and restart
   generations. Exercise Play before clock mapping and before/at/after a fixed
   boundary, including nodes whose musical bank did not change.
4. Verify local pause is exactly silent; old PCM remains audible during slow or
   failed preparation; committed changes share the same mapped boundary. Assert
   bounded slots/queues and lifecycle cleanup. Run normal and ASan/UBSan/leak
   checks without weakening failures or deadlines.
5. Drive three official GSP hosts with independent protocol/band owners and
   actual PCM, in a separate build/runtime. Verify real C pointer callbacks,
   captions, full-width follower Play and normal/reduced motion. Replace only
   external radio/sensors/time adapters; no recreated browser sound/controller.
6. Build pinned-SDK packages with accepted office voicing, diagnostics off.
   Inspect actual configured artifacts, manifests, ELF/source hashes, UI data,
   OTA fit and retained Recovery/bootloader. V2/v3 mixtures must not establish
   cooperative following or commit; local playback stays safe with existing
   recovery feedback. Do not add a second compatibility protocol without review.
7. Only after local/native gates pass, prepare a concrete three-device candidate
   and use applicable user write authorizations. The completed f45936 baseline
   installation is not blanket permission for its later updates. Reconfirm
   calibration-safe placement if the devices have been handled. Roll out with
   devices separated, retain the saved rollback package and verify each identity,
   HEALTHY boot, calibration and output counters before manual three-node tests.

Physical tests still establish user listening/interaction acceptance, not a
calibrated acoustic phase/drift bound or sustained RF/load certification. No
remote repository write, new production dependency or microphone capture.
