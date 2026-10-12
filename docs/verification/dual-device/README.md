# Physical dual-device integration (2026-10-12)

Status: implemented and locally verified; the prepared package is **not installed**.
Both boards still run the previously accepted single-device package
`1d57c5719989`. The outstanding physical step is the user's confirmation that
the boards are separated for startup magnetic calibration. Do not treat the
simulator or host transport tests as hardware acceptance.

## Behavior and integration

The first physical version supports two upright Mosaicos connected horizontally.
The left board supplies its complete committed phrase to the right; the right
composes its own selected role/expression with the existing ArrangeLinked code.
Contact without a phrase locks following/recording in the same way as the desk.
Each board still requires its own explicit Play; attachment never grants loop
permission. Detaching retains the exact committed phrase and existing playback
permission. Root clock establishment is silent; a joined Play waits for a bar.

`board_link.c` uses the existing pinned BSP `mosaico_network`, magnetic classifier
and peer session components. Public callbacks copy events into an independent
worker queue. Radio initialization completes before OTA health acceptance and
audio activation, so startup RF/NVS work cannot suspend the PCM stream.
Magnetometer sampling, decoding and logging run outside the audio owner.
Failed calibration/sensor reads keep the device disconnected; restart separated
to recalibrate. It never pairs devices using radio proximity alone.

The ECHP v1 codec preserves every double, note, harmony, articulation and source
field without C struct padding. Complete plans are bounded to 4,208 bytes;
transfers use paced reliable 44-byte fragments, CRC and version/session/revision
checks. Partial or stale transfers cannot overwrite a committed phrase.
Two-way timestamp samples estimate the peer offset, selecting the lowest RTT
of three samples and refreshing every five seconds. No sample-perfect acoustic
synchronization claim is made.

The audio owner receives copied latest-state snapshots. An inaudible B replica
supplies the upstream plan to local A, so the same C arrangement and synthesis
produce the accompaniment. Periodic clock updates do not re-synthesize unchanged
music or clear pending plans. Actual phase corrections also move queued plan
boundaries; equivalent heartbeat origins leave imminent commits intact. Active
loops remain active when their phase is corrected. Stop joins the link worker
and network callbacks before freeing the device context.

Only components already in the pinned BSP were added. The battery component is
explicitly pinned to the existing `espressif/bq27220==0.1.2`; a resolver-selected
0.1.3 during the first build was removed before preparing the release package.
UI resources, musical algorithms, speaker voicing and Recovery remain unchanged.

## Verification

- `sh tests/link_codec/run.sh`: 11,236 exact round trips, 232 malformed input
  rejections; normal and ASan/UBSan/LeakSanitizer pass. Codec decode uses 80 bytes
  of static stack; large scratch storage is heap allocated.
- `sh tests/board_link/run.sh`: real adapter/codec with transport/sensor substitutes
  passes full phrase transfer, different boot clocks, ACK pacing and failure
  retry, CRC/stale/partial rejection, complete empty plans, callback retry,
  contact gating and safe stop. ASan/UBSan/LeakSanitizer pass outside ptrace.
- `sh tests/linked_device/run.sh`: passes full imported pitches, silent attach
  and rejoin, explicit bar-aligned start, clock refresh, exact detach retention
  and empty-parent recording lock. Normal and sanitizers pass.
- `sh tests/linked_device/run_clock.sh`: passes preparation/pending preservation,
  whole-bar-equivalent PCM, detach during a pending replacement, correction of
  queued bar boundaries, continued PCM during positive correction, and identical
  or equivalent heartbeats immediately before a commit. Tests reproduced the
  boundary/temporary-silence/heartbeat defects before their fixes; final normal
  and sanitizer runs pass.
- `python3 tests/device_runtime/test_runtime.py`: 23/23 pass; the new case verifies
  copied network snapshots, cache access while audio I/O is blocked, silent
  following and retained phrase after detach. Normal and sanitizer runs pass.
- `sh tests/band/run.sh`: controller, actual PCM, recording cues and output
  calibration pass. `sh tests/arrange/run.sh`: 864 plans and 1,552 transitions
  match the frozen reference normally and with sanitizers. The frozen reference
  was not edited. `cmake --build build-host` and CTest: 2/2 pass.
- Official GSP 1.5.1 / GSPC 0.6.1 native backend and desk builds pass.
  `python3 tools/check_desk.py --build-dir projects/echoy/build-sim-dual-device
  --output output/dual-device/desk --duration-ms 60000`: 13 checks pass with real
  C input, full linked phrases and PCM. Zero late blocks, zero clipping, exact
  silence after pauses and normal UI/host cleanup. [Checks](desk-checks.json),
  [PCM evidence](desk-audio-metrics.json). This exercises shared UI/arrangement;
  hardware-specific codec, transport and clock paths are covered separately above.
- Pinned SDK doctor confirms the exact required revision and esp32s31 support.
  `system-update-bundle` run `20261012-122158-system-update-bundle-2` succeeds.
  [Actual package/image hashes](artifacts.json) were computed from
  `output/device-audio-ui-build`, avoiding the runner's stale default artifact
  path. Offline official bundle inspection verifies unchanged retained layout
  and destinations `0x8000`, `0x210000`, `0xf00000`; no Recovery image is included.

The build reports one size warning because the new application is larger than
the **factory Recovery** partition. It fits the intended `ota_0` application
partition; the official bundle targets only that application destination.
Do not use generic IDF flash targets or resize Recovery. Initial CMake regeneration
also emitted include-ownership warnings in the pinned SDK's Wi-Fi/WPA components;
those SDK files were not changed. Earlier sandbox-only dependency-registry and
LeakSanitizer/ptrace failures were resolved by permitted external execution,
without relaxing tests or upgrading the SDK.

## Remaining hardware acceptance and limits

After the separated-board confirmation, install the prepared package on each
authorized Device ID using the official workflow, retaining boot/log/operation
evidence. Verify both reach HEALTHY and log `magnetometer baseline ready`.
Then ask the user to place the boards upright side by side, record on the left,
select a complementary role on the right, and press Play on each. Check contact
identity, received complete phrase, clock RTT/offset, independent Play/Pause,
exact detach retention, rejoin, actual audio deadlines and listening alignment.

This version is two-board/upright/horizontal only. Three boards, rotated seams
and vertical arrangements are not supported by the physical adapter yet.
Committed source edits propagate after wireless transfer and local preparation,
then switch at the next available local bar; a simultaneous same-bar transaction
on both boards is not implemented. Radio/speaker timing, drift, magnetic
enclosure tolerance and behavior under real interference remain unverified.
The invisible upstream replica currently also allocates synthesized buffers;
physical memory headroom still needs measurement. These limits are separate
from the retained Recovery startup watchdog issue already observed on both boards.
