# Physical dual-device integration (2026-10-12)

Status: package **`2822d40910b0` is installed on both boards**. Both real homes,
separated-board magnetic calibration and idle audio passed. The first board
remained on boot `3411308831530673249` at 202.7 seconds; the second remained on
boot `17407228737210624198` at 97 seconds. Both have zero application crashes,
partial writes, write errors and estimated audio deadline misses. The subsequent
physical pair check now confirms contact, phrase transfer, audible playback,
independent pause and detached phrase retention, with qualitative user listening
acceptance. See [physical acceptance](physical-acceptance.json) below; simulator
and host transport results remain distinct from these device observations.

Official update operations are `2c440ef6-d3ef-4dc2-ac66-85aeadace29d` and
`57201513-205f-43f7-97b8-a8cc6558596e`. Identity, ELF image, HEALTHY commit and
unchanged device bootloader checks pass. [Device evidence](device-results.json),
[first home](active-first-home.png), [second home](active-second-home.png).
Before the final first-board write, retained Recovery again had its known startup
interrupt-watchdog reset; metadata was saved and no valid Core Dump was available.
The installer waited for a ready Recovery boot before submitting the update.
The second board's final writer had crash count zero. This retained-firmware
issue was not changed, and both installed application boots have crash count zero.

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

## Physical startup correction

The initial `7951d5c491a0` candidate reached firmware HEALTHY on the first board,
but its first silent 240-frame block took 42.8 ms and tripped the unchanged audio
deadline protection. The second board was not updated with that candidate.
Firmware health alone did not establish usable audio.

Diagnostic builds established that the scheduler was already in internal memory.
Pinning audio to core 1, away from the core-0 Wi-Fi driver, still left a 41 ms
mixing operation, so core placement was not accepted as the explanation or fix.
First-block cycle profiling then measured 11,565,465 cycles in empty cue scans,
2,331,530 in voice work, and 132,705 in clock handling, with zero pending commits.
At the configured 320 MHz, empty cue scans alone cost about 36 ms. Diagnostic
build identities and measurements are in [startup diagnostics](startup-diagnostics.json).
The exact reason that this unchanged old mixer became slower with the radio-enabled
firmware was not isolated; the measured excessive repeated work was removed.

The mixer now gathers active playback/cue slots once per block and refreshes them
after any commit within the block. It preserves slot/addition order and skips
idle voices while still applying identical volume smoothing. Already-equal
volume values need no repeated smoothing arithmetic. No audio timeout, DMA depth,
volume, limiter, synthesis or musical rule was relaxed. The audio owner remains
explicitly on core 1, and bounded timing diagnostics are retained.

`sh tests/render_active/run.sh` compares 96 actual PCM windows bit for bit against
the frozen pre-optimization mixer from `5fa0d8f`, across every role/expression,
future cues, in-block commits/crossfades, volume changes and cancellation. Expected
audible windows must also contain nonzero PCM. Normal and ASan/UBSan/LeakSanitizer
runs pass. The 23 runtime cases, band/calibration checks, clock regressions and
13 native GSP/PCM desk checks were rerun successfully after the optimization.

The final package's measured idle render-plus-conversion maxima are 6,818 us and
6,836 us, both below the unchanged 10 ms audio-block duration. Both boards logged
`magnetometer baseline ready` and remained silent as intended. Internal free RAM
was about 10 KiB per board at these initial idle checks. Connected preparation and
playback were subsequently exercised as recorded below.

## First physical pair acceptance

The first board (`...f452a0`) was physically on the left using Voice; the second
(`...f40c6c`) was on the right using Harmony. Matching RIGHT/LEFT contact sessions
were recorded. The right received full 849-byte snapshots from the left and
committed Harmony plans with the recorded five source anchors; changing its
expression to Broken produced nine events. Both real playback screens were
captured, and nonzero physical PCM output was observed on each device.

The user confirmed hearing them in time with no obvious problem. The user then
confirmed both requested steps: pausing only the left left the right playing,
and physically detaching retained the right phrase and continuous playback.
Logs show the right's amplifier still active after its LEFT-edge detach, with
nonzero sample counts increasing. Subsequent screenshots show both paused with
their phrases retained. Right-side Record permission was disabled while following
and enabled again after detachment. No firmware, UI or playback controls were
changed by the agent during this read-only check.

Official `iris device-status`, `iris logs --snapshot`, a bounded 35-second live
log observation and `iris screenshot` all succeeded. Both stayed on their
installed boot/image with crash count zero, no partial writes, no PCM write
errors and no estimated audio deadline misses. Active render/conversion high
watermarks were 14,901 us on the left and 9,610 us on the right; these are longer
than the earlier idle maxima and are not a claim of every block staying below
10 ms. The configured buffer/late-output protection was not changed.

The logs include three metadata-delivery timeouts and successful detach/reattach
and transfer events; the causes of the earlier reconnects were not established.
Observed clock-estimate RTTs span 13,518–73,914 us; no acoustic offset or long-term
drift bound was measured. Internal free RAM fell to about 3.2 KiB on the left in
one logged sample. Basic interaction/listening acceptance does not establish
long-duration radio/load robustness or ample internal memory headroom.

[Structured observations and user-report provenance](physical-acceptance.json),
[left playing](seam-first.png), [right playing](seam-second.png),
[left after pause/detach](seam-first-detached.png),
[right after pause/detach](seam-second-detached.png).

The home UI currently has no persistent "跟随"/"Following" text. Its actual
following state controls recording permission and receives the upstream phrase;
the user did not overlook a label. A persistent following indicator has not been
implemented or approved in this acceptance step.

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
  Final `system-update-bundle` run `20261012-133513-system-update-bundle-2` succeeds.
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

Installation, baseline calibration and the basic physical pair flow are accepted
within the observations above. Continue future testing from these existing
results; do not repeat the installation or basic listening questions merely to
reconstruct state. Long-running RF/load behavior, quantitative acoustic alignment
and the low internal-memory margin remain to be evaluated.

This version is two-board/upright/horizontal only. Three boards, rotated seams
and vertical arrangements are not supported by the physical adapter yet.
Committed source edits propagate after wireless transfer and local preparation,
then switch at the next available local bar; a simultaneous same-bar transaction
on both boards is not implemented. Quantitative radio/speaker timing, drift,
magnetic enclosure tolerance and behavior under controlled interference remain unverified.
The invisible upstream replica currently also allocates synthesized buffers;
long-running connected/playback memory headroom still needs evaluation. These limits are separate
from the retained Recovery startup watchdog issue already observed on both boards.
