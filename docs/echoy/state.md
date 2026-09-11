# Portable state and sessions

`echoy_core.h` provides a C11 control model for at most three device slots. Slots
are host aliases, not wireless identities. Pass the three configured stable UIDs
to `echoy_core_init`; the hardware adapter must assign unique UIDs and a fresh
boot ID to each `ECHOY_BOOT`. A zero boot ID requests an in-process monotonic
host ID. Readiness callbacks carry that boot ID plus the operation generation
from the latest snapshot. A cancelled, superseded or old-boot callback returns
`ECHOY_STALE`. Firmware must not reuse host fallback IDs across process restarts.

One control task owns `echoy_core_t`. Feed timestamped `echoy_input_t` values to
`echoy_dispatch`, call `echoy_tick` with monotonic integer microseconds, and copy
state with `echoy_snapshot`. Structure fields exist to permit static allocation;
production callers must not mutate them. `clock_ppm` is zero by default; only the
host oracle fixture sets the prototype's +28/-41/+63 ppm clock assumptions.

`ECHOY_BOOT_READY`, `ECHOY_PLAY_READY`, `ECHOY_ROLE_READY` and `ECHOY_SYNC_READY`
mean the corresponding adapter has actually finished preparing. Failure uses
`PLAY_FAILED`, `ROLE_FAILED` or `SYNC_FAIL`. There is no automatic success timer.
The contact confirmation window is 450000 us and the clock hold window is
1800000 us, starting when the adapter reports a heartbeat-derived `ECHOY_LINK=0`.
The 1050000 us sync preparation delay appears only in the host trajectory driver.

`ECHOY_ROLE_REQUEST` starts preparation, `ROLE_READY` queues the role, and
`ROLE_CANCEL` invalidates that operation and its phrase candidate. Role and phrase
memories are committed immediately if the whole session is paused, otherwise on
the common bar boundary. `ACTIVITY` changes visual/live expression immediately;
`FINISH_ACTIVITY` permits an idle commit. A playing session commits density and
onsets only at its bar boundary. `VOLUME` is independent and immediate; closing
`VOLUME_OPEN` does not discard pending parameters. Fault recovery is represented
by setting the relevant fault/loading input to zero; it never changes a paused
transport to playing. A new role request clears the role preparation error.

`CONTACTS` is a complete report of zero to two identified left→right edges.
Invalid endpoints, duplicate or conflicting directions, self-edges, cycles and
reports exceeding three devices are rejected atomically. The adapter must reject
up/down contacts and ambiguous wireless identities before emitting left/right
edges. Discovery alone only updates `discovered`. Confirmed contact creates a
sync generation; after real readiness it joins at a future bar. Member count,
then the reference member's UID, selects the reference session. A failed sync
retains existing transport; new reliable contact/link evidence starts a fresh
transaction. Removing contact changes the response chain at a bar boundary while
retaining membership and clock. Losing the reference elects the lowest surviving
UID without changing epoch/rate. Hold expiration gives the lost member its own
session with nondecreasing phase (less than one microsecond of quantization),
without replaying elapsed bars or starting a paused device.

`echoy_bar_plan` copies a complete caller-owned plan, including each member's
committed motif and actual events. No plan contains pointers into mutable state.
The control task must publish that copy through an owning queue or equivalent
synchronization; an audio task must never read `core.devices` or `core.plan`
directly. Each entry carries boot/session/clock versions, bar and epoch, live
volume/activity and committed activity. The audio adapter can immediately apply
live volume/expression to newly scheduled output using the copied controls;
composition continues to use committed activity. Task 5 supplies that DSP path.

Output faults, pause and zero volume preserve motifs. A data fault or loading
suppresses local events immediately but changes motif availability only on the
next bar; recovery also waits for that boundary. A follower sees the complete
committed motif of its direct left neighbor, even when the neighbor is silent.
If the direct neighbor has no motif, the follower uses its solo vocabulary.

`echoy_feedback_key` exposes UI-independent keys. Boot/loading/error pages precede
normal feedback, with role-loading before data-loading, and role error before
data/output error, matching the prototype status view. Normal feedback priority
is motif recovery, play preparation, synchronization, beat waiting, pending role,
pending phrase, pending activity, moving, unconfirmed contact, pending relation,
notice, link hold/split, then paused/playing source/leader/shared-solo/solo.

## Verification

```sh
cmake -S pc -B build-host
cmake --build build-host
ctest --test-dir build-host -R state --output-on-failure
ctest --test-dir build-host --output-on-failure
node tests/oracle.mjs --self-test
```

`state_unit` exercises exact microsecond boundaries, cancellation, generations,
three-member chains, fault separation, reference election, continuous fallback,
topology rejection and feedback priority. `state_oracle` replays all six frozen
JS trajectories / 21 checkpoints and compares parameter/transport/membership/
discovery/source state and every field of the complete motifs. Only opaque
identity representation and at-most-two-microsecond timestamp quantization are
normalized. Its explicit readiness events replace demo delays; the JS source and
frozen expected cases are not changed. Two fixture setup fields set directly by
the original JS (playing transport) are likewise set directly in the host replay,
while actual play/wait/cancel behavior is covered through dispatched unit inputs.
