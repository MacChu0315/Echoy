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
Verification: `verification/v4/README.md`. The retained v4 comparison preview
uses `build-sim-v4`; do not rebuild its bundle in place while it is running.

### Empty home Record action (2026-10-10)

At the user's request, a tile with no playable phrase now shows one centered
Record action across the existing 244x84 middle strip. Role and Settings remain
84x84. First Cancel and empty recording return to Record only; a real prepared
phrase restores Record + Play. Capture still shows Cancel and playback shows
Pause. This depends on actual plan availability, not a persisted first-run flag.
Following/preparing/fault permissions and the musical model are unchanged.
Separate native Record and Cancel/Pause targets prevent a delayed Cancel from
arming recording. The record glyph uses its existing mask at the center; Play
and the divider are absent until a phrase exists.

Preview: `build-sim-record-only`, desk HTTP 18900, A/B/C 3234/3235/3236. A starts
empty and silent; all older previews remain available. This refinement is not
installed on hardware; the accepted physical package is still `7c08b632adf9`.
Native input checks pass with normal/reduced motion and firmware builds with zero
warnings. See `verification/record-only/README.md` for evidence and restart details.

### Third trial-tap reminder (2026-10-10)

The user chose a reminder after the third trial-pad tap before recording. The
existing status line now shows "点录制，再敲一遍，就能留下节奏。" for four seconds,
with an English equivalent. Only accepted new presses while empty and Record is
available count; holding a press does not add taps. The reminder never records,
saves a phrase, grants playback, or extends its lifetime on subsequent taps.
It appears at most once per running tile. Starting Record or acquiring a real
phrase ends first-use guidance, so cancelling recording does not re-arm it.
No persistence or audio behavior changed.

Latest preview: `build-sim-third-tap`, desk HTTP 18901, A/B/C 3237/3238/3239. A is
left empty with the reminder unused. The record-only preview at 18900/3234–3236
remains available for comparison. Native checks verify timing, both languages,
normal/reduced motion and recording/cancellation; firmware builds with zero
warnings. This remains a preview; installed hardware is still `7c08b632adf9`.
Evidence and restart instructions: `verification/third-tap/README.md`.

### Harmony home refinement

At the user's request, the shared C renderer now spreads the three Harmony home
ribbons from 29 to 44 px vertical spacing, retaining the middle position, curves
and playback animation. Bottom controls retain their exact sizes; role cards,
other roles and rhythm-duration marks are unchanged. The frozen v4 reference is
untouched. The user accepted retaining this ribbon spacing and clarified that
the remaining crowding concerned the short duration marks below it. Those marks
now retain their onset positions but end before the next included onset, with
five pixels between stroke endpoints. This separates overlapping sounding notes
visually without changing the music. Hidden capture layers do not affect spacing.

The user found the separated marks improved, then requested alignment with the
top ink of the Chinese Volume/Simplicity labels. Home mark rows now move down
20 px to local centers 175/181/187: first visible stroke pixels are at screen y263,
exactly matching the two label tops. Gaps and all surrounding geometry remain
unchanged. Three-row chords stay within y263–276; Dust is unchanged nearby.

The Harmony comparison preview uses `build-sim-harmony-mark-height`: desk HTTP 18898,
A/B/C views 3228/3229/3230. The earlier mark-spacing preview at 18897/3225–3227
is retained to preserve the user's new recording, and v4 remains at 18896/3222–3224.
The new A view contains a separate seed5 Harmony phrase2 example for review.
The user accepted retaining the label-top alignment; these UI refinements were
installed with settings package `7c08b632adf9` on 2026-10-10. Art/image and native
GSP input tests pass, including
bottom-control bounds, 33 unchanged cases outside Harmony home, and 54 generated
plans with separate marks for every event. See `verification/harmony-layout/README.md`
for the ribbon change and `verification/harmony-marks/README.md` for the current
mark-spacing and label-alignment evidence and current preview restart command.

## Settings refinement (2026-10-10)

The user approved simplifying settings to Display, Language, How to play, and
About this tile, explicitly excluding "remember the last rhythm". The shared C
UI now presents these four entries on one screen. Home Record and role-gallery
Hear once remain; Export and Put away remain on the outer PC desktop rather than
inside the tile. The four-step guide and device information replace prototype
and experiment prose. Simulator battery readings are labelled as simulated;
hardware uses its existing validated battery view and actual app version.

Display brightness ranges from 20–100%, initially 100%. Automatic dimming is on
by default: 60 seconds of inactivity dims to 20%; touch restores the selected
brightness. Capture, preparation and playback keep it awake. Preferences are
currently per running tile and reset on restart; no rhythm or preference
persistence was added. The PC visual uses native GSP shading; firmware calls the
public BSP brightness API from the UI owner, with no audio-thread I/O or flash
writes. The user confirmed physical brightness adjustment; measured power savings
remain unverified.

Preview: desk HTTP 18899, A/B/C views 3231/3232/3233, build `build-sim-settings`.
A was left on Settings with an empty, silent rhythm. Older previews remain alive
to preserve the user's recordings. The user accepted the new preview and requested
installation. Package
`7c08b632adf9` is now installed on the same verified Mosaico, HEALTHY boot
`16575296128339463175`, operation `b3127241-392a-4b71-af96-b00912b8dcfa`.
Image identity, healthy acknowledgement, commit, bootloader and partition checks
pass; the actual home screen is normal. Accepted speaker parameters from
`7c431dacfd59` are unchanged. The user then confirmed the physical settings pages,
touch and visible brightness adjustment are all normal. Automatic idle dimming
was validated in the simulator; measured power savings remain unverified.
Verification and restart commands are in `verification/settings/README.md`.

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

## Current device audio (retained)

The .60-ceiling trial package was `44ad4e4a59ec`, HEALTHY boot `11202007909105101612`.
It retains hardware90, gain6, all role gains, UI and synthesis; only the device
peak ceiling changes from .90 to .60. Image/commit/retained-layout verification
and the actual home screenshot pass. The user reported markedly improved
distortion, but all four roles were too quiet, prompting the next trial below.
Playback seed `8234` reaches the new PCM ceiling 19,660 with no clipping,
partial writes, output errors or estimated deadline misses.
The user explicitly approved a follow-up device-only voicing trial: attenuate
frequencies below about 120 Hz, adjust compression and role balance while keeping
the .60 ceiling, and prioritize louder/less-distorted playback over retaining
the original bass weight. Rhythm and pitch remain unchanged; frozen HTML/shared
synthesis remain the reference. This authorizes the bounded timbre change and
its installation on the same board under the existing update authorization.
The audio follow-up was installed as package `7c431dacfd59`, HEALTHY boot
`6467976503804649253`. It adds a device-only second-order 120 Hz high-pass,
uses PCM drive18 with the same .60 ceiling/1 ms lookahead, and reduces limiter
release to 40 ms. Musical trims are 1.6/.52/.0317/2.38; default-volume median
digital RMS increases are about 3 dB across roles versus the preceding trial.
The common output changes also affect cues, whose scheduling is unchanged.
Startup identity/commit/retained-layout and home checks pass. Physical listening
found no obvious distortion, but the user would prefer more loudness. A further
local scan found gain18 -> gain48 gives only .75 dB median Harmony RMS at full
volume, versus 8.52 dB for Dust, so no additional gain-only package was installed.
Retain the audio profile established in `7c431dacfd59`. That package's logs confirm
Harmony phrase 2 (Broken), and the user subsequently explicitly confirmed that
phrase 3 (Answer), the original problematic expression, also has no obvious
distortion at the same higher volume. Physical listening follow-up is closed
for this trial. On 2026-10-10 the user decided to retain the current volume
("音量上就先这样吧"). Keep the output parameters from `7c431dacfd59`;
the current UI package is `7c08b632adf9` with those same parameters;
stop further loudness tuning unless requested. This is provisional retention,
not a claim of measured acoustic performance or the absolute hardware limit.
Any future substantial loudness improvement needs acoustic/output-chain
measurement or a separately evaluated spectral adjustment.
288 calibrated cases have no clipping and true peak <= -3.9 dBFS; 120 raw synth
banks are unchanged. Filter/mean-level regressions, PCM/band tests and 22 runtime
cases (normal and sanitizer) pass. The official build has zero warnings and
15% app partition headroom. See `verification/speaker-voicing/README.md`.

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
