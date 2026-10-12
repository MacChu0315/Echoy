# Hardware link adapter tests

Run `sh tests/board_link/run.sh` from the Echoy repository. The test compiles
actual `board_link.c` and the canonical plan codec against the pinned BSP public
headers. Radio, timer, FreeRTOS and sensors are replaced; two independent link
instances exchange the actual wire packets. A controllable asynchronous owner
models readiness, command consumption and future activation. Real compilation,
synthesis and PCM activation are covered by
`sh tests/cooperation/run_transactions.sh`, which couples this protocol to two
real band owners, their immutable preparation jobs and actual PCM rendering.
That script also accepts a single case name, for example `authorization-ack-lost`.
Set `TRANSACTION_TEST_CFLAGS` to the same sanitizer flags below to sanitize it.

Coverage includes proposed full-plan transfer before commit, delayed preparation,
common root-bar activation with different local clocks, paused participants,
deferred recording admission with the latest source, baseline/no-op source joins,
empty plans, a revision-zero cold join before initial preparation, the first
recorded/example phrase after two empty devices join, CRC repair, stale sessions and mixed fragments, duplicate packets,
delivery identity, sending errors, withdrawn readiness, revised local candidates,
latest source supersession, owner callback backpressure, delayed owner consumption,
dropped ARM/COMMIT, preparation failure, 30-second timeout, final-authorization
partition recovery, physical contact gating, radio loss versus magnetic release,
and checked shutdown retries.

Address/undefined-behavior checks:

```sh
ASAN_OPTIONS=detect_leaks=0 \
LINK_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
sh tests/board_link/run.sh
```

The local sandbox runs under ptrace, where LeakSanitizer cannot operate. Disabling
its leak scan does not disable the address or undefined-behavior checks.

The adapter starts the product-specific radio network synchronously before
`board_link_start` returns. Its worker owns magnetometer I/O, plan encoding,
reassembly, clock sampling and logging. Complete views, plans and byte buffers
are heap-owned (explicit PSRAM on ESP), outside the 7 KiB worker stack. Keep both tiles separated at boot
until the `magnetometer baseline ready` log. A sensor initialization, calibration
or read failure leaves linkage unavailable until restart. Only complementary
RIGHT/LEFT contacts with relative rotation zero are accepted; radio proximity is
insufficient.

The v2 private protocol transfers canonical complete proposals with CRC and
session/transaction identity. Owner read-back acknowledges BEGIN/OFFER readiness,
ARM freezing, COMMIT authorization and actual boundary activation separately.
The leader observes remote ARMED before issuing its own ARM and observes remote
authorization before issuing its own COMMIT. Commands remain in the copied desired
snapshot until owner read-back acknowledges them. A right-local edit may withdraw
READY before ARM; it cannot silently reuse an obsolete prepared revision.

Preparation and transfer have a 30-second ceiling. Both prepared participants
negotiate a root-clock bar at least one second ahead; missing authorization fails
closed 200 ms before that bar. A partition after final authorization can reach
only one device, so the protocol explicitly enters recovery with bounded waits;
it does not promise perfect atomic delivery across a partition. Deferred receivers
are not participants and cannot indefinitely hold the source's independent work.
Neither receiving a plan nor authorizing a joint change grants looping permission.

At most one reliable application message is outstanding per device. Each 44-byte
plan chunk advances only on its matching delivery event and transfer generation.
Metadata repeats every 500 ms; readiness/control status every 100 ms. Delivery
failure backs off 750 ms and restarts an incomplete snapshot. Clock synchronization
uses three two-way samples, rejects RTT above 80 ms and uses the best RTT sample;
it refreshes every five seconds and freezes the mapping during arm/commit. These
are estimates, not evidence of sample-exact physical playback synchronization.

`board_link_stop(timeout_ms)` must succeed before callback context is released.
Timeout or network shutdown failure preserves the singleton for retry. The timeout
bounds worker join; the BSP synchronous network shutdown has no timeout parameter.
Host results do not validate magnetic classification, radio delivery, physical
clock asymmetry/drift or sound; those require the two actual devices.
