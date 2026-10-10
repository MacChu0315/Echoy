# Echoy current state

The approved source is now `reference/v4/echoy-arrange-B-v4.html`; hashes are in
`reference/v4/SHA256SUMS`. The delivered ZIP and v3 historical baseline remain
unchanged. The v4 musical/audio/arrangement code matches v3. Preserve the design, musical rules
and explicit per-device playback permission. Dependency pins remain in
`dependencies.json`. No remote write has occurred. The first authorized physical
installation and its follow-up UI fix are recorded below.

## v4 presentation

On 2026-10-09 the user explicitly accepted the current v4 GSP simulator effect.
Treat this as approval of the simulated presentation and interaction baseline.
It does not authorize device writes or establish physical hardware acceptance.

The single-line identity, raised pad/sliders and 84/244/84 bottom row now follow
v4. Native hit targets switch independently of the visual transition; capture
uses one Cancel target, playback/preview uses one Pause target. Stale Cancel
cannot arm a new recording. A pure C CSS ease-out curve includes shortened
reversal timing, and fixed glyph masks provide alpha/scale without replacing
native controls or dynamic status text.

The user explicitly confirmed touch/mouse acceptance; do not claim native
Tab/Enter focus support. The desk propagates system reduced-motion changes to
all three C views. Linux native loopback ACK hints and the direct host's high
simulation tick remove RPC stalls; hardware uses the shared C view/controller.
Verification: `verification/v4/README.md`. Current live preview uses
`build-sim-v4`; do not rebuild its bundle in place while it is running.

## PC application

The audible three-tile entry is `tools/desk_preview.py` (HTTP 18896, native GSP
views 3222/3223/3224). A starts on the desk; B/C start silent on the shelf.
It runs the real portable C synthesis, ArrangeLinked/ArrangeModel and band
scheduler. Browser code controls placement and embeds official GSP screens;
it does not synthesize sound or substitute a separate musical model.

Audio runs on a shared sample timeline independently of GSP rendering. Each
GSP handle has its own owner thread. Immutable preparation jobs execute outside
the central state lock and stale revisions are discarded. File export/status I/O
also occurs outside the audio lock. Playback output uses installed `paplay`.

Confirmed behavior includes tap audition, recording cues, repeated identical
recordings, one-shot replies, linked full phrases/pitches, independent looping,
boundary changes, exact detach retention, fault recovery and remove/restore.
New neighbors never auto-start, replaced automatic-preview jobs stay cancelled,
and language selection updates all three product screens.

Verification and concrete limits are recorded in `verification/audio-linked/README.md`.
The supplied HTML remains the oracle, including actual Chrome offline sound.
Do not call the earlier Vibe single-screen UI-only entry an audible acceptance
run. It remains useful for isolated UI regressions.

## Single-device firmware

The physical entry now connects the shared band to a FreeRTOS audio/control
owner and a lower-priority immutable synthesis worker. UI callbacks enqueue
ordered actions and copy a cached view. Startup and explicit error recovery stay
silent; overflow, partial writes and missed estimated output deadlines stop
playback. The loading view contains valid phrase metadata before worker startup.

Application-owned `board_audio` uses public BSP power/I2C/pins and public
ES8311/I2S interfaces: TX only, 24 kHz mono, 16-bit, 3x120-frame DMA, 40 ms write
deadline, actual accepted-frame counts, and queued-sound clearing on cancellation.
Do not also initialize the BSP's cached speaker/I2S facade in this application.
The sample clock is an estimate until measured on hardware.

`board_health` polls the BSP battery on its own worker with checked reads and
freshness/validity handling. Unknown readings display `--%`; only validated
present-battery zero triggers empty. A previously confirmed empty state stays
stopped across failed reads. IMU is explicitly unknown rather than a mock 0°.
Shared power/I2C initialization precedes concurrent peripheral workers.

Local firmware and the official system-update bundle build for the pinned
ESP32-S31 SDK. Verification and the final artifact are recorded in
`verification/device-preparation/README.md`. These are local checks with host
hardware substitutes, not physical-device acceptance.

## First authorized physical installation (2026-10-09)

The user authorized installing bundle `38b79718eed8` on `/dev/ttyACM0`. The
verified board is Mosaico v1.2 / ESP32-S31, Device ID
`4553502d49524953010030eda0f452a0`, with retained Recovery 0.1.2 and layout
`mosaico-retained-recovery-2m-v1`.

The first attempt hit an interrupt-watchdog reset in Recovery while application
erase overlapped Recovery UI/network initialization. Read-only reconciliation
confirmed a reboot into Recovery and no commit record. A controlled retry of the
same authorized bundle after Recovery finished initialization succeeded. The
official updater verified Echoy's ELF identity, HEALTHY event, commit ID and
partition hash, with unchanged bootloader hash. This supports a startup-overlap
hypothesis but does not prove the watchdog's low-level cause.

The first UI binding fault (261 for unknown-battery fill visibility) was fixed
and package `afae9df52f28` was subsequently authorized and installed. That UI
reached home, but delayed automatic firmware acceptance overlapped streaming and
audio timed out at boot. After the user tapped Restore, playback stayed active;
the user recorded a phrase, navigated pages and confirmed audible but very quiet
sound. These are user observations, not measured acoustic acceptance.

The current local implementation completes firmware acceptance after UI setup
and before explicitly activating the audio worker. Delayed auto-accept is
disabled. Hardware volume is now 60 instead of 40; accepted nonzero PCM/peak/PA
and write-duration diagnostics are retained across faults. The first approved
audio package `87197fd80c0d` was written but failed UI setup with queue timeout
263; it correctly withheld HEALTHY and audio activation.

A native fixture measured 27 initial component changes: SDK queue depth 24
reproduces the timeout; 64 passes. Current defaults and compile-time guards
require 64 and manual acceptance. The user authorized `874d650babab`; it installed
successfully and remained on the same healthy boot for over five minutes with
zero UI errors, audio write errors or estimated deadline misses. Recording,
one-shot playback and return to idle were observed in the device logs.
Tests: 16 runtime cases and
22,263 audio assertions, including ASan/UBSan/LSan, plus the real GSP fixture.
See `verification/device-audio/README.md` for hardware history and artifacts.

On installed `874d650babab`: render maximum 2,201 us, write maximum 8,893 us,
owner stack minimum 8,728 bytes, zero partial writes, and sustained nonzero PCM
with PA enabled. Peak PCM was 4,368/32,767 (about 13% full scale). The user still
reported very quiet sound at hardware volume 60. The user authorized and installed
volume-only package `d87ae02e9222`: hardware volume 90 removes another 15 dB of
attenuation, retaining the UI slider and all logic. Official update verification
matched the image, HEALTHY event, commit ID and unchanged bootloader/partition
hashes. The real home screen and startup logs are normal, with no observed UI or
audio errors. The user subsequently reported very quiet sound even with the UI
slider at maximum, including Voice/Dust rather than only bass-heavy Pulse.
This rejects practical loudness acceptance of the current volume-90 image.

Read-only investigation of that boot found peak PCM 6,725/32,767, nonzero output,
PA requested on and no write/deadline errors. Official V1.2 schematics confirm
the pinned BSP's MCU TX GPIO52 connects to ES8311 SDIN; no pin change was made.
Driver-accepted samples and a PA software flag do not prove analog output level.
Codec register values have now been read from the device, as recorded below.

The user authorized and installed diagnostic package `bb7f0a3dd05f`, with hardware gain still
90 and unchanged UI/music. `CONFIG_ECHOY_AUDIO_DIAGNOSTICS` defaults off. Enabled
builds expose private RPC 0xECA0: 1 reads cached status, 2 explicitly requests a
fixed 1-second 1 kHz / 8,192-peak PCM reference, 3 cancels. The owner pauses music,
reads ES8311 registers before/after the reference, and never resumes music
automatically. Any UI action interrupts the diagnostic. Exactly one authorized
reference completed: both register masks 127, ID 0x83/0x11, volume register 0xBC,
mute 0x60 before and 0 afterward, 24,000 tone frames, no errors. The user heard it
clearly and substantially louder than the phrases. This confirms working output
at that known signal level; it is not a measured frequency response or SPL.

The shared prototype normalizes loop RMS to at most .045, then applies a .45
master. Measurements across 144 representative plans give RMS .0151–.02025 at
maximum UI volume, about 19–21 dB below the continuous reference. Shared synthesis
and HTML remain unchanged. A device-only linear gain of 3 (+9.54 dB) is now installed
before PCM conversion, with clipping telemetry. All 144 loops and 2,304 tested
ten-taps/second mixtures avoid clipping; worst calibrated peak is .920. Runtime
and sanitizer checks pass. Package `3f156df66112` disables temporary diagnostics
and was explicitly authorized and installed successfully on boot
`1222412418091812715`. Official image/HEALTHY/commit and retained-layout checks
passed; the home screenshot is normal. Idle startup telemetry confirms gain 3000
milli, clipping 0, output errors 0 and deadline misses 0. Playback telemetry also shows nonzero PCM, peak 14,414, no clipping or output
errors. The user reported louder output but still too quiet, and repeated that
assessment after moving the UI slider to maximum (confirmed by screenshot).
The gain correction is installed, but practical loudness acceptance still fails.
See `verification/quiet-audio/README.md` for the evidence and test scope.

A pulse phrase on `874d650babab` took 9,553,429 us to prepare. On the gain-3 boot,
a subsequent role preparation took 42,284,433 us. The performance work below
addresses this delay. ESP-Iris remote
pointer injection is unavailable in the current mirror adapter; interaction
evidence above comes from physical user touches.

## Responsiveness (2026-10-10)

Performance was verified on package `22fa6931ffb3`, with HEALTHY boot
`14291928129683081475`, matching ELF identity and unchanged retained Recovery
layout/bootloader. Sound output settings remain hardware volume 90, gain 6 and
peak ceiling .90.

The instrumented baseline measured 10–49 s synthesis, 7.4–7.5 fps even on a silent
Pulse home, and 1.3–1.4 fps on active Harmony. Fixes remove per-sample software
double/transcendental work from normal synthesis, preserve precise filters at
extreme legal Grain pitches, cache oscillator data per job, avoid static
repainting, reuse invariant geometry, and cache the current dynamic art across
GSP partial surfaces. Scratch storage belongs to each UI instance instead of
occupying the render task stack. The same C implementation remains portable.

The first optimized hardware run prepared Pulse in .31 s, Voice in .79–.82 s,
Dust in .42 s and Harmony in .60–.62 s (different recorded rhythms from baseline).
The user confirmed both preparation and UI were markedly smoother. Active
complex art still ran at 4–5 fps and render stack headroom reached 1,284 bytes,
which motivated the final raster cache and scratch-storage correction.
The final package boots to the expected home. The user reported normal screens
and controls, smoother than before. Final captured Pulse playback: .30–.34 s
preparation, 14.4 rendered fps, UI gap about 80 ms, render stack minimum 7,660
bytes, no clipping/audio write errors/deadline misses. Do not call this 30/60 fps,
or quote Pulse measurements as final Harmony measurements.

Tests pass: 99 HTML audio references with unchanged budgets; 384 full-pitch
comparisons to prior C; audio behavior/ownership/performance and sanitizer checks;
1,026,120 art pixels unchanged; cache/geometry/blend and independent QOI decoding;
21 runtime cases and full official GSP pointer flows in an isolated build.
See `verification/performance/README.md` for scope, measurements and artifacts.

## Remaining physical integration

The retained output calibration was accepted provisionally on `4ee7991a5d56`: hardware volume 90,
device PCM gain 6 with .90 peak limiting and diagnostic RPC disabled. The
one-reference-playback authorization has already been used.
The user rejected gain-3 musical loudness even at confirmed maximum UI volume. On
2026-10-10 the user approved trying device-only loudness gain with peak limiting,
including its effect on musical dynamics. They also explicitly authorized all
subsequent Echoy upgrade-package installations on this same verified board,
without per-package confirmation. Preserve Recovery and verify each artifact and
target; do not reinterpret this as permission for unrelated devices or remote
Git writes. The limiter was built, inspected and installed successfully on boot
`15517228967953464410`; verified image identity, HEALTHY, commit and retained
layout all passed. The actual screen reached home. Local checks cover 144 plans
and 2,304 mixtures with no clipping; RMS increases 3.25–6.02 dB over gain 3.
21 runtime tests pass normally and with sanitizers. The device-only processor
adds 1 ms lookahead and clears its tail on pause/fault/reopen. Physical recording
and repeated playback/pause were observed: peak 29,490, gain reduction active,
zero clipping/output errors/deadline misses, render+conversion maximum 3,211 us.
On 2026-10-10 the user chose to keep the current volume for now ("音量就先这样吧").
Retain these output parameters; stop further loudness tuning unless
requested. This is provisional retention, not a claim of complete acoustic-quality
acceptance. See `verification/loudness-limiter/README.md`.
Use the official Mosaico workflow; generic IDF flash targets point at retained
factory Recovery and are blocked. Let Recovery finish initialization before
submitting updates on this board; do not blindly replay an uncertain operation.

Previous paired-match package: `ea1b10c28cd4`, verified HEALTHY boot
`16482991358666837799`. It retains the performance changes, hardware volume90,
global gain6 and peak ceiling .90. Musical-bank gains are Pulse/Voice 1,
Dust .0672 (about -23.45 dB), Harmony 2.38 (about +7.53 dB); cues are unchanged.
Actual boot logs confirm the new coefficients. The worker also reports each
prepared bank's real peak before/after applying its gain, outside the realtime
output loop. Image/commit/retained-layout checks and the home screenshot pass;
no startup audio write errors or deadline misses were seen.

The .22-Dust/1.1885-Harmony trial was rejected as Dust still louder than expected
and Harmony too quiet. Two further user listening selections used seed `1128`:
Harmony at nominal .65 matched Voice lowered to about .324 (knob y213), giving
`1.188502*.65/.324324 ~= 2.382`. Using that Voice reference, Dust was lowered to
the desired relative level at about .099 (knob y238), giving
`.22*.099099/.324324 ~= .067222`. These are quantized screenshot estimates and
user listening provenance, not acoustic SPL measurements. Both results were
installed together. The user reports relative volume is acceptable, but Harmony
has audible clipping/distortion; sound-quality acceptance therefore remains open.

192 compiled-profile cases include the actual matching seed: no clipping,
max sample peak .899990844447, max measured true peak -0.8 dBFS. The 72 prior
Pulse/Voice WAVs are unchanged. Band/calibration tests, sanitizers and all 22
runtime cases pass; official firmware build/install pass. Do not substitute
numerical LUFS equality for this board-specific listening calibration.
See `verification/role-balance/README.md`.

This boot logged a BQ27220 profile-block read timeout during startup. The existing
BSP then rewrote its battery profile, verified all 26 fields, exited CFGUPDATE
and reported the battery online. Battery code was not changed here; retain this
recovered startup observation for later battery validation.

Next: validate single-device touch, speaker, battery and
resource watermarks on hardware. Then implement/accept physical neighbor
discovery, transport and clock synchronization on two and three boards. Device
export still has no hardware destination; IMU integration is also unfinished.
Do not repeat the earlier mistake of treating UI-only or single-tile tests as
completion of the whole musical prototype.

## Current audio follow-up

Current device package is `44ad4e4a59ec`, HEALTHY boot `11202007909105101612`.
It retains hardware90, gain6, all role gains, UI and synthesis; only the device
peak ceiling changes from .90 to .60. Image/commit/retained-layout verification
and the actual home screenshot pass. The user reports markedly improved
distortion, but all four roles are too quiet; loudness acceptance remains open.
Playback seed `8234` reaches the new PCM ceiling 19,660 with no clipping,
partial writes, output errors or estimated deadline misses. Next evaluate
higher average output within this ceiling before another hardware trial.

The resumed investigation recovered the missing volume evidence: Harmony Answer,
seed `40aa`, density .5 had been at full volume. The user still heard distortion
at an observed .640 volume, during playback with no increase in limiter counts
and no output errors. At .342 they reported markedly less/no distortion, then
selected .486 as the highest clean level for this phrase (inferred peak ~.655).
This supports level-dependent board output distortion, but does not identify a
particular analog component or establish a universal acoustic limit. Computer
listening was unavailable. Exact-seed C versus frozen HTML checks pass 9/9.

The .60 ceiling is a conservative listening trial with headroom below that
selection; it may change dynamics and relative loudness at strong peaks.
240 calibrated cases pass with no clipping, peak <= .599993897 and true peak
<= -4.2 dBFS; raw synthesis banks are unchanged. PCM and band checks pass,
including the reported-seed regression that fails on the old ceiling. Runtime
checks pass 22/22 normally and with ASan/UBSan/LSan outside the ptrace sandbox.
The official pinned-SDK bundle build/install pass. See
`verification/harmony-headroom/README.md` for evidence and limitations.
