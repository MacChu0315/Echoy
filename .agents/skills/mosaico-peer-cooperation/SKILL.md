---
name: mosaico-peer-cooperation
description: Use when implementing or debugging physical linking, shared state or synchronized behavior across Mosaico devices, including detach, reconnect, stale messages and topology changes. Not for standalone applications or generic Wi-Fi setup.
---

# Mosaico peer cooperation

Make product permissions and physical membership explicit before changing a
protocol. Inspect the pinned contact classifier, transport and application
adapters. Preserve approved behavior; unresolved choices that materially change
device behavior require a concrete proposal, not assumptions from another app.

## Define independent state

| State | Establish it from |
| --- | --- |
| Physical contact | Local sensor/classifier readiness and current observations |
| Radio availability | Transport observations and freshness |
| Group membership | Product join/release rules and session identity |
| Shared data | Committed revision, candidate ownership and validation |
| Common clock | Mapping, uncertainty, epoch and negotiated execution time |
| Local permission | Explicit product/user action, independent of synchronization |

Specify what each retains or clears on physical detach, radio-only loss, Pause,
reboot and failed preparation. For example, a product may retain a phrase while
releasing its clock group; another may clear content. Do not universalize that
choice. Clock exchange or reconnect must not implicitly override a retained
local Pause unless the approved product explicitly defines that behavior.

## Check physical capacity and asynchronous boundaries

Count active peers and simultaneous sender/receiver roles in the actual adapter.
A middle node in a chain needs both directions. Three simulated tiles sharing
one host clock do not demonstrate three physical nodes with independent clocks.

Fence delayed messages and completions with boot/session/transaction identities
and cancellation generations. Define ownership before publication, readiness
before common activation, and failure behavior for already committed content.
Check ABORT before BEGIN/OFFER, duplicate commits, stale packets after release,
simultaneous first starts and clock updates after an execution time is fixed.
Bound queues/retries and preserve control-message capacity during bulk transfer.
Do not turn every heartbeat into a new retry or extend deadlines without cause.

## Validate software and physical behavior separately

Host tests should execute multiple actual controller/transport/scheduler instances
with independently controlled clocks. Inject delay, loss, duplication, reordering,
reboot and boundary cancellation. Observe real output scheduling as well as UI.
Then exercise native inputs and the physical topology within authorized scope.

For magnetic linking, establish startup placement and current-boot calibration
from the target BSP's requirements. A cached disconnected state or zero edges
does not prove a calibration-safe magnetic field. If devices have moved, freshly
confirm placement before a calibration-dependent restart. Keep radio proximity
distinct from contact; investigate environmental or sensor evidence before
loosening classification thresholds.

Report supported physical topology, firmware pairing, exercised transitions,
clock/output measurements and untested mixed-version or sustained-load cases.
Human observations of alignment are useful acceptance, not measured phase error.
