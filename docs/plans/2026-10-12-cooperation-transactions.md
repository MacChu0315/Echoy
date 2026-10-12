# Echoy cooperation boundaries and shared updates

## Approved scope

Implement the user's four confirmed decisions against frozen v4 HTML. Keep
`reference/v4/echoy-arrange-B-v4.html` unchanged. `ArrangeModel.compile/stage/commit`
and `ArrangeApp.refresh` (including `Promise.all`) remain the musical/commit
reference. Detach keeps the current cooperative phrase; there is no restore-own
entry or cross-reboot memory. Preserve independent playback/pause, no join
autoplay, audio calibration, three preparation seconds and four recording beats.

## Implementation and ownership

- Shared band: retain recognized connections separately from applied musical
  edges. A newly joined receiver in count-in/recording, its automatic preparation
  or its one-shot reply defers the edge until that operation ends. Cancel/pause
  still ends the local operation; detach clears the deferred join. Latest valid
  upstream state is used on admission, never a stale disconnected session.
- Shared band/arrangement: add externally gated preparation and explicit future
  staging, leaving the existing HTML-equivalent automatic path for the PC desk
  and independent edits. Keep committed banks playing until a validated common
  commit. New configurations received after a revision is armed are queued for
  the next revision; pause/volume/cancel remain immediate and cannot be reversed
  by a later commit.
- Radio worker: versioned, session-bound proposal/ready/arm/commit exchange.
  Transfer the proposed full phrase before the leader commits it. Readiness is
  reported only after the audio owner has accepted prepared buffers. Confirm
  the receiver's arm before commit authorization; acknowledgements/retries are
  idempotent. A 30-second preparation/transfer ceiling bounds stalled work;
  commit negotiation uses a future shared bar with a one-second lead and fails
  closed before a missed boundary. Actual disconnect/failed preparation aborts
  the obsolete shared revision; committed audio and explicit playback permission
  survive. Normal connected operation commits both affected boards at one bar;
  a partition during final authorization cannot be advertised as perfect atomic
  delivery and must enter a visible recovery state.
- Owner/worker interface: `echoy_coop.h` defines copied snapshots and idempotent
  commands. Large proposed/current plans stay in heap storage, off small task
  stacks; network/I2C/serialization never run on the PCM owner. Deferred receivers
  report that they are not participating yet, so their recording cannot hold the
  leader's independent work indefinitely.
- Shared UI: `echoy_control_view_t.cooperation` drives connected, waiting for left
  phrase, join-after-recording, following-left and recovering text. Keep the
  existing geometry/button sizes; combine recording progress with the deferred
  join message in the existing status area. Regenerate scene/font resources from
  `tools/generate_scene.py`; reuse existing font packs (maximum 32).

## Work and verification

- [x] Add failing deferred-join tests: both empty, count-in, active recording,
  preparation/reply, cancel, pause, detach, rejoin and old callback rejection.
- [x] Implement shared-band gating and the copied owner/link interface.
- [x] Implement and test the session-bound wireless preparation/commit exchange,
  including delayed/duplicated/dropped messages, preparation failure, paused
  participants, concurrent edits and disconnect near a proposed boundary.
- [x] Implement cooperation status text and generate resources; verify with real
  native GSP input/screens, including empty and capture states.
- [x] Exercise two independent real C controllers, actual synthesized PCM and
  the transport state machine; assert common activation time and old-plan
  preservation rather than only packet acknowledgement.
- [x] Run targeted band/runtime/arrangement/codec tests, sanitizers, native GSP
  checks and pinned SDK build; verify frozen-reference hashes and artifact/layout.
- [x] Install sequentially on the two already authorized Device IDs using
  `mosaico.py`; obtain separated placement before startup magnetic calibration.
  Verify identity, healthy boot, baseline, speaker deadlines and resource use.
- [ ] Ask the user for specific new-boundary actions: join during recording,
  cancel and detach while deferred, then shared edits while both play and while
  one is paused. Retain evidence and update current project state accurately.

The plan records the already-approved scope and implementation choices; it is
not a second approval or a stopping point. No remote writes are authorized.
