---
name: mosaico-realtime-input
description: Use when a Mosaico sensor-driven application needs shared Host/device processing, recorded-input replay, fresh-sample tracking or deterministic animation timing. Not for a static interface or tuning a sensor driver without an application input pipeline.
---

# Mosaico real-time input and replay

Keep sensor observations, processing state and presentation time distinct. Use
the target project's public BSP driver for calibration and complete-frame
assembly; share the production processing/behavior C implementation between
Host replay, native GSP and firmware. Confirm the actual sensor rate: subpages,
packets, reads and complete samples may have different rates.

## Establish the input contract

Specify sequence/epoch, capture and receive times, units, array shape/order,
validity and coordinate direction. Only a successful **new complete sample**
may advance observation evidence. Failed reads publish a fault or age state;
retained pixels are not a new sample. Duplicate identity and redraws must not
increase sample counts or refresh last-valid time. Keep no-target distinct from
invalid/stale input, including at startup before any valid sample.

For frame schemas, display transforms and trace contents, read
[the frame contract](references/frame-contract.md).

## Put processing before lossy presentation

Assign a source owner to driver observations, detector and behavior evidence.
Process every required sample/fault **before** a latest-only display mailbox.
A render owner can copy and age its own state, but never feed that copy back
into acquisition. Coalescing display snapshots may hide a transient fault;
retain the event trace rather than claiming every fault was shown.

A source faster than a slow display must still acquire/maintain a target. Test
that scheduling order with the actual production wrapper, not just the detector
called directly at ideal timing. Keep platform adapters thin; rendering consumes
a prepared pose, never resubmits sensor samples.

## Make time transitions reproducible

Inject a monotonic clock. Before accepting a newly received capture, expire
previous continuity/stillness/deadline evidence at the relevant time, then
normalize the new result at that same receive time. An event API should return
an already aged result; a same-time tick must be idempotent. Held coordinates
are not fresh observations of a stationary target.

Specify reset/epoch, pause, restart, source outage and clock rollback semantics.
Reset source identity, processing, behavior and seeded animation together on
replay. Native pause/redraw changes no logical time or sample evidence. Sleep,
wake and similar transitions consume production result validity; tentative or
stale coordinates cannot manufacture a confirmed target. Timing and thresholds
come from this product, not the source application's constants.

## Replay and acceptance

Prefer existing recordings; retain original capture/receive/fault ordering and
gaps. Feed complete arrays through production C; saved coordinates are a result
oracle only. Never uniformly resample or omit faults silently. Label generated
input synthetic and preserve its generator/seed; label recordings recorded and
retain acquisition configuration and scene-label limitations. A simulator is
not assumed to emulate the physical sensor.

Compare sample/event counts and per-event states, identities, timestamps and
coordinates. Exercise no target, motion/stillness/loss, multiple candidates,
duplicates, brief/long gaps, pause/redraw and replay consistency as relevant.
Check the native application's actual pixels and transitions separately from
Host results and authorized physical observation. Report unlabelled scenes and
missing real-world evidence explicitly.
