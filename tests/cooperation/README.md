# Cooperation regression tests

Run from the Echoy repository:

```sh
sh tests/cooperation/run_deferred.sh
sh tests/cooperation/run_transactions.sh
```

Both scripts accept one case name, for example `link-abort-deferred` or
`wireless-empty-cold-start`.

`test_deferred.c` exercises the real band, arrangement, recording model,
immutable preparation jobs and PCM scheduler on a bounded simulated timeline.
Its 24 cases cover count-in, capture, preparation and one-shot reply admission;
Cancel/Pause; detach/rejoin and stale jobs/sessions; empty recordings and empty
upstreams; cooperative phrase retention; and the independent A→B→C desk chain.
Direct link-owner cases require ARM/COMMIT after deferred admission, reject
aborted offer payloads, preserve later jobs under repeated ABORT, retain the old
playing PCM when preparation fails, and prevent failed recordings from replying
with an older prepared bank. Assertions include recorded input, cancellation
generations, initial silence, actual audio, explicit playback permission and
Record/Play availability.

`test_transactions.c` connects two independent real band owners to the actual
`board_link` protocol. Its 18 cases cover common-bar activation with playing or
paused participants, failed preparation, lost authorization, concurrent edits,
queued changes after ARM, deferred recording admission, and the first phrase
after two empty devices connect. Recovery cases acknowledge an aborted partial
transfer without repeated owner rejection, resume after fresh peer/owner recovery
without a parameter change or physical replug, deduplicate requests, preserve
queued edits and Pause, and retain old PCM when a failed ABORT replaces an
unconsumed source BEGIN. Unoffered ABORT cannot cancel an older authorized commit
or a deferred recording job. Only radio, OS, clock and sensors are replaced.
See [the adapter test notes](../board_link/README.md) for protocol coverage and
physical limitations.

Address, undefined-behavior and leak checks use the same cases:

```sh
DEFERRED_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
sh tests/cooperation/run_deferred.sh
TRANSACTION_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
sh tests/cooperation/run_transactions.sh
```

LeakSanitizer requires execution outside a ptrace sandbox. When that is not
available, `ASAN_OPTIONS=detect_leaks=0` still runs address and undefined-behavior
checks but does not verify leaks. Host tests do not establish physical sound,
magnetic contact recognition or sample-exact synchronization on devices.
