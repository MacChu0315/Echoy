# Task 8 report — device settings, diagnostics and control owner

Status: DONE_WITH_CONCERNS. Base: `15bce98`. Branch: `feat/echoy-mosaico`.

## Delivered

- Portable checked settings storage, battery observation, immutable frame
  lifecycle, diagnostic formatting/injection validation and single-owner control.
- Real application NVS namespace `echoy`, schema-1 fixed 64-byte settings blob,
  CRC/range/finite validation, committed role/phrase/activity memories, independent
  volume/locale, 500 ms debounce and retained failed writes. No runtime session,
  playing state, pending work or boot identity is stored. Defaults are Chinese,
  Pulse and paused. No sysmeta erase or any flash erase fallback.
- BSP BQ27220 `state_of_charge`, retained valid charge on failure, low charge
  feedback and verified open-drain `bsp_power_set_shutdown(true)` on real valid
  0%. Development battery injection cannot actuate the shutdown pin.
- Existing-driver-only IMU handle and cached module manager identification. No
  module manager start, EEPROM scan, slot claim, IMU streaming or camera init.
- Actual initialized utils `esp_iris_rpc_register` service 0xec01: read-only
  bounded JSON snapshot method 1, developer UI/locale/diagnostic methods 2–4
  compiled and registered only with `CONFIG_ECHOY_DEV_INJECTION=y` (default off).
  Identity/session/clocks/edges/revisions/audio/heap/task/drop/error/local-device
  fields are included. No pairing-token API is called or logged.
- Actual `esp_iris_screen_register` RGB565 backend: one on-demand aligned PSRAM
  full frame, controlled `lv_snapshot_take_to_draw_buf` under the LVGL display
  lock, immutable chunk reads, fresh capture on the next pull frame, idempotent
  end/free. Confirmed real Iris service end calls on stop/completion/disconnect.
- Runtime starts the real radio/contact/audio components. UI/RPC use a 16-entry
  zero-wait command queue; radio uses the existing 32-entry queue. A 10 ms owner
  task exclusively mutates core/protocol/settings. UI updates use copied state
  under a nonblocking lock; speaker receives immutable copied plans.
- Local speaker failure/retry maps to OUTPUT_FAULT, never DATA_FAULT. NVS, UI,
  radio or codec failure does not abort already available Iris/Recovery. Product
  healthy marking moved out of unconditional main startup and waits for owner,
  UI and actual successful speaker status.
- Trusted physical/committed-member handoff and bounded parameter requests reuse
  PREPARE with documented flags=1. The existing nine wire kinds and flags=0
  transaction remain unchanged. Exact state schema, fragment flags/order,
  UID/boot/session/sequence/transaction and trusted source are checked. Unknown
  flags and non-PREPARE flags reject; flag 1 never enters the commit state
  machine. HELLO alone never authorizes. A physical/established witness may
  introduce real UID/boot bindings; indirectly learned uncommitted bindings are
  not vouched onward and indirect boot replacement is rejected.
- Coordinator assembles own-member requests and witnessed directed edges,
  commits through standard PREPARE/READY/COMMIT, and installs immutable core
  state. Heartbeat expiry dispatches LINK; after the existing hold split the
  owner rebuilds surviving protocol membership while retaining phase mapping.
- Audio plans use a private 40 ms future copy, including an already received
  COMMIT at the future boundary. Product core remains unchanged until apply.
  An independent monotonic audio clock version cancels unrendered forecast
  notes when the forecast/clock is withdrawn, even when returning to the old
  underlying core clock. Already queued BSP DMA samples remain outside that
  cancellation API, as documented by Task 5.
- Added `docs/echoy/hardware-acceptance.md`, extended `protocol.md`, enabled
  LVGL snapshots, and added local module-manager component discovery. The
  dependency lock changed only its generated manifest hash, no versions.

## RED/GREEN evidence and host coverage

1. `platform_services` initially failed its Pulse/phrase/Harmony-memory defaults
   assertion against the empty implementation. It now covers exact persistence,
   paused restore despite playing/pending source state, bad schema, NVS read/write
   errors with pending retention, invalid NaN settings, battery errors/invalid
   sensor values/low charge/zero, and 100 repeated mirror begin/read/end cycles
   with balanced allocations, repeated end, refresh reads and capture failure.
2. `platform_control` initially failed the READY/paused startup assertion. With
   owner implementation, bounded queue overflow and output-fault mapping passed.
   Its first two-device join remained RED until the fake transport used 1 ms
   queue delivery and the scenario respected the existing 20-second clock
   stability window (the initial 15-second expectation was invalid). It now
   passes physical two-device admission, physical 2+1 trusted handoff without
   preauthorizing all identities, a nonleader role commit, leader loss/rebuilt
   surviving two-member state, HELLO-only strangers, unknown flags, request
   replay, wrong boot and exclusion of owner requests from protocol commit.
3. `platform_diagnostics` failed its snapshot-format assertion against the empty
   implementation. It now tests bounded JSON/truncation, required field presence,
   credential absence, release-disabled injection and invalid action rejection.
4. Added a pending-COMMIT forecast regression: the ordinary-copy forecast failed
   the expected future role while the real owner remained unchanged. Installing
   the received future COMMIT into the private copy made it pass. This exposed
   and corrected a runtime handoff that would otherwise publish after the DMA
   presentation deadline.

## Final validation

- `cmake -S pc -B build-host` and `cmake --build build-host -j8`: PASS.
- `ctest --test-dir build-host --output-on-failure`: **27/27 PASS**, 22.00 s.
- Configured `/tmp/echoy-task8-asan` with
  `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie` and linker
  `-fsanitize=address,undefined -no-pie`; build PASS.
- `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest
  --test-dir /tmp/echoy-task8-asan --output-on-failure`: **27/27 PASS**, 46.83 s.
  LeakSanitizer alone remains disabled for the previously established sandbox
  ptrace limitation. AddressSanitizer/UBSan were enabled; no findings.
- After final local-device JSON field additions, `platform_diagnostics` was
  rebuilt and rerun in both normal and sanitizer builds: **1/1 PASS** each.
- Activated existing `/home/zhuzhengming/esp/esp-idf/esp-idf-echoy-v6.2-dev/export.sh`
  then `idf.py build`: PASS for the actual ESP32-S31 runtime and APIs. Initial
  dependency metadata lookup was blocked by restricted network; exact read-only
  registry/build escalation was auto-review approved. No remote writes occurred.
- Development configuration build using `/tmp/echoy-task8-dev-sdkconfig` with
  injection enabled: PASS. `riscv32-esp-elf-nm` on `libechoy_platform.a` showed
  `inject_rpc`. Restored normal `build/sdkconfig` (`CONFIG_ECHOY_DEV_INJECTION`
  unset), rebuilt successfully, and nm found **no inject_rpc symbol**.
- Final normal IDF build has zero warning/error diagnostics; image is roughly
  3.84 MB (see current build artifact for exact generated size).
- `git diff --check`: PASS. No sibling source/dependency source, remote state,
  hardware flash or credentials were changed.

Logs: `/tmp/echoy-task8-test-final.log`,
`/tmp/echoy-task8-asan-test-final.log`, `/tmp/echoy-task8-idf-final.log`,
`/tmp/echoy-task8-idf-dev.log`.

## Concerns / physical acceptance still required

1. No hardware has been flashed in this task. Real magnetic identification,
   2+1 physical admission, codec playback, battery/power pin behavior, screenshot
   appearance, repeated disconnect allocation baseline and audio continuity
   during PSRAM/UI/NVS contention still require the documented hardware checks.
2. The unchanged clock estimator needs a 20-second stable sample window after
   authorization; standard transactions reserve two bars. Host tests respect
   those bounds. Immediate subsecond real-device joining is not claimed.
3. The BSP still exposes the actual 6 x 240-frame DMA, not the requested 4 x 256
   hardware configuration. Its configured 30 ms lead and the 40 ms forecast
   margin need actual measurements. A late partition/abort cannot retract PCM
   already written into inaccessible BSP DMA. Arbitrary-partition atomicity is
   not claimed, as in Task 7.
4. Requests trust physically admitted/existing members to report their own
   contacts and binding provenance. This is not cryptographic attestation of a
   compromised admitted sender. Older flags=0-only builds reject flag 1 and
   need updating for the autonomous owner request/handoff path.
5. IMU/module diagnostics report unavailable if no existing driver/manager is
   initialized, deliberately avoiding side effects. Module cache is observed
   at startup; no autonomous EEPROM/slot ownership is added for hotplug display.
6. Runtime services are application-lifetime singletons. Frame begin/end and
   startup-before-task failure free their resources; task/network/audio normal
   teardown is reboot/Recovery, not an undocumented hot-restart API.
7. Leak checking was not performed because of the inherited LeakSanitizer
   ptrace limitation. Host sanitizer and allocation-balance tests are not a
   substitute for real device heap/watermark measurements.

## Fix round 1/5 — queue handoff timestamps and immutable capture sessions

Status: DONE. Base: `603dec9`.

1. **Fresh per-packet processing timestamps.** A new stateful fake keeps the
   callback queue live during its drain: receiving TIME/PREPARE causes responses
   to be appended and consumed in the same drain, with each receive timestamp
   later than the loop's initial time. With the old loop-start timestamp passed
   through, `platform_radio_drain` was RED at the first late HELLO's successful
   receive/discovery assertion. The runtime now calls the portable queue-handoff
   function for each packet; it samples `esp_timer_get_time()` for that packet,
   preserves a monotonic lower bound of receive/previous/core/protocol time, and
   carries the resulting time back into the owner's later tick. The low-level
   protocol's invalid-time validation remains intact. GREEN exercises actual
   HELLO, TIME_REQ, TIME_RESP, PREPARE and COMMIT state transitions, physical
   two-device then 2+1 admission, nonleader role change and reference loss, while
   checking every owner tick remains monotonic. It explicitly observes packets
   appended by callbacks during the drain, rather than testing only a prefilled
   queue or comparing the helper's arithmetic to itself.

2. **One immutable frame per begin/end session.** The previous `consumed` flag
   incorrectly treated reaching the final byte as the end of a capture session.
   Updated fake capture generates different bytes for every new capture. The
   test was RED when reading the tail followed by the head returned a new frame.
   Removed read-triggered recapture and the consumed flag. Every valid offset,
   including head/nonzero/tail repeats after end-of-buffer, now returns the same
   captured bytes until explicit `end`. Only the next explicit `begin` captures
   a new frame. GREEN covers 100 sessions, tail-first/head/middle/repeated/full
   reads, unchanged capture count, idempotent disconnect-style repeated end,
   invalid reads after end, no-allocation capture failure, and failure after
   backend allocation with balanced cleanup. The old refresh-read test and
   hardware acceptance text were corrected; the earlier report's offset-zero
   recapture statement is superseded by this immutable-session contract.

Validation:

- `cmake -S pc -B build-host`; `cmake --build build-host -j8`: PASS.
- `ctest --test-dir build-host -R platform_ --output-on-failure`: **4/4 PASS**.
- `ctest --test-dir build-host --output-on-failure`: **28/28 PASS**, 29.99 s.
- Reconfigured/rebuilt `/tmp/echoy-task8-asan` with unchanged address/undefined
  sanitizer flags, then `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
  ctest --test-dir /tmp/echoy-task8-asan --output-on-failure`: **28/28 PASS**,
  58.93 s; no ASan/UBSan diagnostics. The prior ptrace/LeakSanitizer limitation
  remains; no leak-checking claim is added.
- Activated the existing IDF export and built with
  `-D SDKCONFIG=/tmp/echoy-task8-dev-sdkconfig`: PASS; `riscv32-esp-elf-nm` on
  `libechoy_platform.a` confirms `inject_rpc` exists in the development build.
- Restored the ordinary `build/sdkconfig`, built with `idf.py build`: PASS;
  the ordinary configuration leaves injection disabled and the platform archive
  has no `inject_rpc` symbol. No compiler warning/error diagnostics.
- `git diff --check`: PASS. No hardware flash, remote writes, sibling changes,
  sensor/storage behavior changes or deferred diagnostic-field expansion.

Logs: `/tmp/echoy-task8-r1-test.log`, `/tmp/echoy-task8-r1-asan-test.log`,
`/tmp/echoy-task8-r1-idf-dev.log`, `/tmp/echoy-task8-r1-idf-release.log`,
`/tmp/echoy-task8-r1-dev-symbol.log`.
