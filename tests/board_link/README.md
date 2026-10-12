# Hardware link adapter tests

Run `sh tests/board_link/run.sh` from the Echoy repository. The test compiles
the actual adapter and plan codec against the pinned BSP public headers, with
fake radio, timer, FreeRTOS and sensor hardware. It exercises complete plan and
clock exchange, changing loop metadata without retransmitting the plan, empty
plans, CRC/stale/session/mixed-fragment rejection, serialized delivery and retry,
callback backpressure, contact gating, negative mapped origins and stop retries.

Address/undefined-behavior checks:

```sh
ASAN_OPTIONS=detect_leaks=0 \
LINK_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
sh tests/board_link/run.sh
```

The local sandbox runs under ptrace, where LeakSanitizer cannot operate. Disabling
its leak scan does not disable the address or undefined-behavior checks.

The adapter synchronously starts the product-specific radio network before
returning from `board_link_start`. Its worker owns magnetometer I/O, encoding,
reassembly, clock sampling and logging. No complete plans are on its 7 KiB stack.
Keep both tiles separated at boot until the `magnetometer baseline ready` log.
An initialization, calibration or read failure leaves linkage unavailable until
restart. Only confirmed RIGHT/LEFT complementary contacts with reported relative
rotation zero are accepted; radio proximity is insufficient.

At most one reliable application message is outstanding per device. A 44-byte
plan chunk advances only on its matching delivery event. Metadata is sent every
500 ms; failure restarts the latest complete snapshot after 750 ms, and missing
or rejected reception requests a fresh snapshot. Clock synchronization uses
three two-way samples, rejects measured RTT above 80 ms, and selects the smallest
RTT in each group. It refreshes every five seconds. These are clock estimates,
not evidence of sample-exact physical playback synchronization.

`board_link_stop(timeout_ms)` must return success before releasing callback
context. Timeout or network shutdown failure preserves the singleton for retry.
The timeout bounds the worker join; the BSP's subsequent synchronous network
shutdown has no timeout parameter.

Host results do not validate physical magnetic classification, radio delivery,
real clock asymmetry/drift, or sound. Confirm those with the two actual devices.
