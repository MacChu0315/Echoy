# Echoy current state

The approved source is now `reference/v4/echoy-arrange-B-v4.html`; hashes are in
`reference/v4/SHA256SUMS`. The delivered ZIP and v3 historical baseline remain
unchanged. The v4 musical/audio/arrangement code matches v3. Preserve the design, musical rules
and explicit per-device playback permission. Dependency pins remain in
`dependencies.json`. Firmware history remains local. The first authorized physical
installation and its follow-up UI fix are recorded below.

## Reusable development skills (2026-10-15)

Five portable guides are indexed in `../.agents/skills/README.md`: GSP parity,
runtime performance, peer cooperation, audio validation and release evidence.
Their validation record distinguishes document/application-scenario checks
from hardware tests. The GitHub distribution is scoped to skills and usage
documentation; application history, firmware, device records and existing local
work are excluded. These guides do not change accepted device behavior or
transfer Echoy-specific permissions to other projects.

Published to the private `MacChu0315/Echoy` repository on branch
`skills/mosaico-development`, independent root commit
`3e1face2f393d0fa261f5c028c0213809b140dd8`. Remote SHA and its nine Markdown files
were verified after the single push. This application repository has no remote;
the export checkout is `/tmp/echoy-skills-publish-20261015`.

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
empty and silent; all older previews remain available. This refinement was
installed with reminder package `86406d956f52` on 2026-10-12.
Native input checks pass with normal/reduced motion and firmware builds with zero
warnings. See `verification/record-only/README.md` for evidence and restart details.

### Third trial-tap reminder (2026-10-10)

The user chose a reminder after the third trial-pad tap before recording. The
initial status-line version showed "点录制，再敲一遍，就能留下节奏。" for four seconds,
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
warnings. The user explicitly requested installing this wording on 2026-10-12.
Package `86406d956f52` is now installed, HEALTHY boot `9542740490602219176`, operation
`ca0930e9-fa6c-4215-8045-99b05123c247`. Identity, healthy/commit, bootloader/partition
checks and the real single-Record home pass. Audio parameters are unchanged.
The user confirmed the reminder appears, but found the bottom text too small and
requested a larger automatically dismissed popup; the installed follow-up is described below.

Before the write, old Vibe Mode 0.1.2 had one startup interrupt-watchdog reset;
logs and crash metadata were preserved, with no valid Core Dump. A stable Recovery
boot was verified before the single successful update. This Recovery issue is
not fixed by the application UI change.
Evidence and restart instructions: `verification/third-tap/README.md`.

### Large reminder popup (2026-10-12)

Following physical feedback that the status-line text was too small, the same
third-tap reminder now appears in a native 300x128 panel centered over the pad.
Chinese uses two lines of 22px medium text (previously 13px); English reuses the
existing 18px bold font. The original wording, third-tap trigger and once-per-run rule remain. The lifetime
is now three seconds per the timing refinement below. The panel has no input handler, so trial
sound continues and the bottom Record action remains available. Recording or
obtaining a phrase dismisses it. No music or persistence changes were made.
Native normal/reduced-motion tests and actual shared C audio checks pass, including
continued sound while the popup is visible; the pinned SDK bundle builds with
zero warnings. Package `a859d454627f` was installed on the same verified Mosaico,
HEALTHY boot `11406880910400298043`, operation
`a0daa59b-0a52-4577-8b40-eb147e3be642`. Identity, commit, bootloader/partition checks
and the actual Record-only home pass; the same new boot remains normal after a
minute with no UI or audio write errors. Physical readability feedback is pending.
Evidence is in `verification/reminder-popup/README.md`. Existing live previews still show the
previous inline reminder; the new simulator build is `build-sim-reminder-popup`.

### Three-second timings (2026-10-12)

The user requested a three-second popup and a recording countdown starting at
"预备-3". The popup now expires after three seconds. Preparation uses three
one-second counts (3, 2, 1), with matching guide cues; recording still uses four
625 ms beats (2.5 seconds). The existing 60 ms audio scheduling lead is retained,
so capture starts 3.06 seconds after the Record event. UI, model and scheduler
share the preparation constants; the guide copy now says three seconds.
`AGENTS.md` reflects this explicit user-approved departure from the frozen
prototype's two musical preparation beats. No rhythm format, synthesis, speaker
profile or persisted data changes were introduced.

Model and cue timing regressions, 22 device-runtime tests, normal/reduced-motion
native input and the standalone three-device audio regression pass. A concurrent
desktop run recorded one late audio block while all tiles were idle; the identical
binary passed alone without relaxing checks. Host scheduling cause is not proven.
Package `9ef37e48c8b0` was installed on the verified Mosaico, HEALTHY boot
`14075077765010003089`, operation `ddd0b874-0860-4676-b5d2-3762bfe27524`.
Identity/commit/retained-layout and actual home checks pass; the same boot remains
normal after 77 seconds with crash count zero and unchanged audio parameters.
Physical timing feedback is pending. The simulator build is `build-sim-three-seconds`;
no new long-running desktop was started. Evidence: `verification/three-seconds/README.md`.

### Consistent default expressions (2026-10-12)

The user requested consistent initial selection. All roles now start at their
first/left expression: Pulse Accent, Voice Short phrase, Dust Grains, Harmony
Short chords. Harmony no longer initially selects the middle Arpeggio expression.
Per-role manual selections are still remembered within the running session;
restart restores the defaults. Standalone model, shared arrangement (including
initial Harmony tile B) and hardware loading cache use the same defaults.
No musical algorithms, speaker profile, timing or persistent format changed.

Model/shared-band memory checks, 864 reference linked plans and 1,552 reference
state transitions (normal and sanitizer), 22 device-runtime tests and all native
Chinese/English role pages pass. The frozen HTML is intact; only the oracle test's
initial defaults are adapted to this approved change. Package `1d57c5719989` is
installed, HEALTHY boot `5127593241566643399`, operation
`d6e52c8b-6412-424c-9d9b-f51e848ba5e2`. Identity, commit, retained-layout and actual
home checks pass; the new application remains normal after 100 seconds with crash
count zero and unchanged speaker parameters. All four gallery defaults were
verified in native GSP; physical gallery switching was left to the user.

Old Recovery had another startup interrupt-watchdog reset before the write. The
helper preserved crash metadata (no valid Core Dump), waited for a stable ready
Recovery boot and submitted one successful update. The Recovery issue remains
unresolved and is separate from the application change. Evidence:
`verification/default-expressions/README.md`. Simulator build:
`build-sim-default-expressions` (no new long-running desktop started).

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

Display brightness ranges from 20–100%, initially 100%. The original settings
package dimmed after 60 idle seconds; on 2026-10-13 the user explicitly requested
removing this behavior and its switch. Current source keeps the selected level
through idle, touch, capture and playback; see the quality work below. Preferences are
currently per running tile and reset on restart; no rhythm or preference
persistence was added. The PC visual uses native GSP shading; firmware calls the
public BSP brightness API from the UI owner, with no audio-thread I/O or flash
writes. The user confirmed physical brightness adjustment; measured power savings
remain unverified.

Preview: desk HTTP 18899, A/B/C views 3231/3232/3233, build `build-sim-settings`.
A was left on Settings with an empty, silent rhythm. Older previews remain alive
to preserve the user's recordings. The user accepted the new preview and requested
installation. Package
`7c08b632adf9` was installed on the same verified Mosaico, HEALTHY boot
`16575296128339463175`, operation `b3127241-392a-4b71-af96-b00912b8dcfa`.
Image identity, healthy acknowledgement, commit, bootloader and partition checks
pass; the actual home screen is normal. Accepted speaker parameters from
`7c431dacfd59` are unchanged. The user then confirmed the physical settings pages,
touch and visible brightness adjustment are all normal. Automatic idle dimming
was validated in the simulator; measured power savings remain unverified.
Verification and restart commands are in `verification/settings/README.md`.

## Recording response, brightness and sound investigation (2026-10-13, in progress)

The user requested cancellation of idle brightness reduction, improved frame
rate and further Harmony distortion work. Suspected missed taps occur after
the screen already shows recording. Do not treat this as count-in confusion.

Manual brightness now remains at its 20–100% setting through idle, touch and
playback; the idle-dim switch is removed. Native GSP checks retain 60% through
61 s idle and 61 s playback, and the manual slider still works. Redundant home
art redraws on unchanged pointer samples are removed; new grid hits still
redraw immediately. Coverage/pitch-cache optimizations have zero differences
over 3,712 frames / 99,716,368 pixels. Bottom geometry is unchanged. Current
recording/countdown, musical rules and sound parameters are preserved.

Both authorized devices accepted background-home package `09275b935ad8`, ELF
`8c180bbaf1b34e6cf2e2dd1ca961c5a78347ffcbbd125b90a2ef647cc8cd4d5d`.
Left HEALTHY boot `15237885666942643151`, right `9386692617368392907`; exact
operations/retained-layout checks are in `verification/background-home/device-startup.json`.
Both actual homes and separated magnetic calibrations passed. The later
left-only Harmony trial is recorded below; the right retains this ordinary
package. This superseded
diagnostic package `73058c98b7ce`, whose initial checks remain in
`verification/capture-quality/device-startup.json`. Sound quality is still open.

The package includes optional bounded touch diagnostics and private read-only
ECA1 snapshots. Record arms the touch rings; finish/Cancel freezes evidence
until the next recording. There is no automatic trace dump or extra controller
read/ACK. Use `tools/capture_touch_trace.py <new-output> --device-id <id>` and
`tools/capture_audio_snapshot.py <new-output> --device-id <id>` via official CLI.
The first candidate `606c0563f4e0` was briefly installed only on the left; an
independent review found a reset/IRQ statistics race. It was fixed with a failing
then passing deterministic regression before installing the current package.
Do not use the superseded candidate's touch counters as evidence.

Initial live plan/chunk and inactive-trace reads work. The official task-memory
endpoint returns HTTP 501 in this configuration; no dedicated Iris service
stack watermark is available. Both devices retain roughly 25 KB free internal
heap at the checked idle point. Current background-home log captures are
`output/background-home-{first,second}-live-20261013.jsonl`, with bounded
900/600-second lifetimes; do not assume they are still running in a later turn.
No new audio profile has been installed. The right-side current linked
Harmony/Arpeggio reproduction is now captured at exact .65 volume: ten events,
six source notes, following/playing, committed revision 6 with no pending job.
Its full ECHP, state and original-C stage replay are preserved in
`verification/harmony-stages/device-{plan.echp,snapshot.json,report.json}`.
The user heard no obvious distortion in either the average-level-matched
pre-limiter or actual-output WAV. This shifts the next investigation toward
device output level, without identifying a component or claiming a cure. The
right was playing while linked on the previous diagnostic firmware. The user lowered only its volume and
selected a highest-clean position: exact `0.12222222222222229` with the same
ECHP SHA256. The replay at this level has no effective limiting; snapshot and
report are `verification/harmony-stages/device-clean-{snapshot,report}.json`.
This phrase-specific listening point is not a universal acoustic ceiling and
no sound fix has been installed. The user subsequently paused/separated both
boards before the background-home installs; the recorded sample is preserved
locally and was not restored automatically after reboot.

On `73058c98b7ce`, the user still perceived missed responses. The first captured Voice rhythm had
an unknown physical tap count: five observer presses, all submitted and retained
as five source notes (`seed=2894`), no same-grid merges/trace drops/overwrites.
Its maximum interval between controller reads was 226183 us, while reads took
at most 710 us and raw-to-observer delivery at most 2345 us. Do not infer a
physical missed-tap count from this unknown-count rhythm.

A follow-up explicitly counted six taps: all six observer presses were accepted
and retained as six source notes (`seed=9294`). No same-grid merges or diagnostic
loss occurred. Reads were at most 149755 us apart and lasted at most 742 us;
raw-to-observer delivery was at most 174 us. This confirms that one counted
sequence was preserved, not that fast-tap reliability or visual response is fixed.
Full evidence: `verification/capture-quality/physical-input.json`. Both windows
use the same current left Boot ID. Preserve the current six-tap phrase.

The user clarified that missed response means delayed sound or visual feedback
at the moment of tapping. Voice's additional segment-scan optimization is
locally complete: 100800 to 7640 visited segments, identical pixels and about
15% lower paired PC render time; it is included in the current package. On this
evidence the user explicitly approved moving home-art rasterization to a
background producer through the existing GSP Canvas API, preserving the sole
touch reader and musical action path. Implementation, native/ESP producer
tests and eleven UI integration cases pass with sanitizers. Actual native GSP
passes five full-UI, ten input, six cooperation and thirteen real-C audio/desk
groups; 25 static home screenshots match the prior version exactly. The approved design is
`plans/2026-10-13-responsive-home-rendering.md`; do not ask for design approval
again unless a material requirement changes.

On the new left firmware the user recorded six Voice taps and reported that
immediate response seems better. All six observer presses are stored
(`seed=1954`), with no same-grid merge, trace drop or overwrite. All 34 observed
controller IRQs were handled in separate reads (previous counted test: 32 IRQs,
19 reads, 13 multi-IRQ windows). Maximum latest-IRQ-to-read age was 46506 us;
raw-to-observer maximum 238 us. Do not call the 228328 us maximum read gap a
latency: IRQ-only reads are intentionally absent during quiet gaps. Hardware
finger-to-sound/display latency and faster tapping remain unmeasured.
Evidence: `verification/background-home/counted-recording.json`.

The user also confirmed left Play/Pause and then the silent Harmony home are
normal. No audio deadline misses, partial writes, write errors or reboots were
observed. The worker's minimum unused stack was 9200 bytes for Voice and 3648
after Harmony; left internal free after that check was 25271 bytes. Both
boards were paused and separated again before updating the right. On 2026-10-14
the user confirmed the requested same-version cooperation check is normal:
silent initial following, explicit playback, right Pause remaining effective
while left expression changes, with no reported stop/reboot/desynchronization
or spontaneous sound. Fresh live queries retain both installed Boot IDs with
zero crash counts and no output/partial/deadline errors in current telemetry.
This is user acceptance and status evidence, not a timed replay of each action.
See `verification/background-home/physical-acceptance.json`.

The remaining audio task is Harmony output distortion and, as reopened below,
greater useful loudness across all roles/expressions. On 2026-10-14, after clarifying
the right Mosaico's onboard microphone will observe its own speaker, the user
explicitly approved the bounded trial in `plans/2026-10-13-output-measurement.md`:
at most two explicit runs, each at most 12 seconds, local results only. The
diagnostic is implemented and locally verified. Both allowed runs completed:
216000 TX/RX frames in 9.044084 s at input gain 0 dB, then 252000 frames in
10.543422 s at gain 12 dB with a longer silent settle. Neither had I/O errors or
ADC clipping. The user then clarified that people were continually talking in
the office and they were not listening closely. Do not use these statistics as
controlled acoustic evidence or a basis for new sound parameters. The user
cannot leave the office; defer listening-dependent validation until convenient.
No production sound profile has changed; Harmony distortion remains unresolved.

The user later resumed focused listening and reported distortion both on the
desk and while holding the left tile, with lower sounds worse. This makes desk
contact alone insufficient to explain the complaint. After digital comparison,
the user approved the stronger Harmony-only 500 Hz bass-cut trial (thinner bass,
about 2 dB lower average PCM for the preserved phrase), initially on the left
only. Implementation is complete under the default-off
`CONFIG_ECHOY_HARMONY_BASS_TRIAL`; see `plans/2026-10-14-harmony-bass-trial.md`.
The first left-only trial was package `abcdec27b6fe`, ELF
`0020c2b944e8c77039df96ad67807128dcbf308b1b418aba7fc3b3483bfce1d7`,
Boot ID `8257068652410574677`; startup is healthy, calibrated and silent with
zero crashes. Both microphone/reference-tone diagnostics are disabled. The
right retains ordinary `09275b935ad8`. Numerical, worker/job ownership, common
bar and paused-update tests pass. The user reports improvement and accepts the
thinner timbre, but a small amount of distortion repeats at the same sound in
each loop. It is not yet accepted as a complete fix. The post-feedback snapshot
is paused at UI target volume 1.0, Harmony/Arpeggio, seed `8534`, 11 events/six
source notes, with no preparation error. Current-boot logs have zero crashes,
partial writes, write errors or deadline misses. The current exact plan is
preserved in `verification/harmony-bass/residual-plan.echp`.
The user reports residual distortion at approximately 80%; exact readback is
UI target .7611111111111111, with the same plan SHA, revision and Boot ID.
Actual-C replay including the installed filter reaches the .60 ceiling there;
raising it to full increases average PCM by only .504 dB. This does not identify
the physical cause. Do not keep thinning the accepted timbre without new
evidence. See `verification/harmony-bass/README.md`.

The user explicitly reopened loudness tuning for all four roles and all three
expressions, targeting ordinary office listening on a desk at 30–50 cm.
Lowering the final volume alone does not satisfy this goal. A 578-case offline
audit finds clear digital headroom for Dust, modest additional average output
for Voice, and little benefit from simply increasing Pulse/Harmony gain.
Digital RMS is not acoustic loudness or a role-balancing target. Hardware audit
found no obvious pinned-S31 I2S format error; actual amplifier supply/loading
and clean speaker output remain unmeasured. Raising hardware volume would
increase drive after the digital limiter and is not established as a solution.
The user approved the all-role voicing direction, with a left-only first trial;
see `plans/2026-10-14-office-speaker-voicing.md`. The opt-in implementation keeps
Source/Foreground, original normalization target and plan bytes unchanged.
Fixed per-note harmonic phases reduce the extra waveform crest found in the
first two offline candidates. The first office profile used role gains
`1.6/.78/.0634/1.6`, retaining common drive 18, hardware volume 90, .60 ceiling
and Harmony's 500 Hz cut. Pulse/Voice/Dust show higher weighted digital level;
Harmony is near parity (some cases slightly lower) with more upper harmonics.
Limiting remains stronger than the original profile: do not claim an acoustic
fix or all-role physical loudness gain from these figures.

The first office trial installed on the left was `7307f8ed9a3e`, ELF
`133f0e68982ff07bd35be1ac76f1dddefe5bc8df76e8bc3bc0e99f0f4f9a29c9`,
Boot ID `1063869759841904881`. Identity/HEALTHY/retained-layout and actual home,
silent startup, zero crashes and magnetic baseline are verified. Right remains
ordinary `09275b935ad8`. Device microphone/reference diagnostics are off.
Local DSP/band/runtime tests and sanitizers, 152 byte-identical prototype-to-
production FULL/PCM comparisons, 13 native GSP/audio groups and ordinary/trial
pinned-SDK builds pass. The native test uses one combined desktop PCM chain,
not separate physical speakers. The user now accepts Pulse, Voice and Dust
timbre/loudness and reports no obvious Harmony breakup, but rejects Harmony as
especially loud, sharp/bright and too electronic. Retain the other three roles.
The after-feedback snapshot is
paused Dust/Grains at UI 1.0, seed `95a8` with seven source notes, not a snapshot
of the earlier Harmony listening state. Current boot logs retain zero crashes,
write/partial/deadline errors, owner render max 3966 us and preparation stack
minimum 7948 bytes. The last preparation is 612138 us at the later Dust state,
not a maximum Harmony workload measurement. Mixed-version cooperation still
needs physical checking. Evidence: `verification/loudness-review/README.md`.

Left now runs the Harmony-only softening package `5d4776c9ae6d`, ELF
`aacb24e1acc4e0a4f11a7cb10b08645f4d0a6e930e98ff19e9ae1675d79a01c7`,
current Boot ID `1456888021128116879`. Harmony retains its 500 Hz cut and fixed
phases, with fundamental/second/third weights `.60/.20/.10`, added fourth/fifth
removed and its trim reduced to `.8`. Pulse/Voice/Dust remain byte-identical to
the accepted office version. Actual production/PCM comparison passes 295 cases
(77 selected Harmony, 218 other roles), plus targeted DSP/band/runtime,
13 native GSP groups and the pinned-SDK build.

The successful installation's first boot `15367590955867785783` rejected a
stable magnetic baseline (`right_y=917..919`, no classified edges). This was
not an app crash; the cached paused/unconnected state had not proved a safe
physical field. After the user separated the boards at least 15 cm and manually
restarted left, the current boot reports baseline ready at 2114 ms, unchanged
ELF, zero crashes and silent startup. No second firmware write was needed.
`AGENTS.md` now makes that placement-check distinction explicit. The user has
confirmed the new Harmony is suitable, has no obvious breakup, and explicitly
accepted the changes. Together with the earlier Pulse/Voice/Dust acceptance,
this closes the current sound-tuning round. Preserve the accepted profile in
future device builds with `CONFIG_ECHOY_OFFICE_VOICING_TRIAL=y`; do not silently
return to the older output settings because of the flag's historical name.
Fresh read-only logs retain the same left ELF/Boot ID with zero crashes,
partial writes, write errors and deadline misses after listening. Owner render
maximum is 4048 us, write maximum 8591 us and preparation stack minimum 7956
bytes; latest preparation is 591155 us, not a worst-case bound. This is user
listening acceptance and runtime evidence, not calibrated acoustic measurement.
Recording that acceptance made no sound/firmware change. The user then explicitly
requested right-device synchronization. Both devices now run accepted package
`5d4776c9ae6d` with the same ELF. Right operation
`4956865b-08b5-45dd-868d-d238535aedd6`, Boot ID `7939481319926026302`:
HEALTHY/validated, retained Recovery/layout, actual home, silent startup and
magnetic baseline ready at 3493 ms pass, with zero crashes/output errors.
The left was not rewritten; its later observed Boot ID is
`11284810966805748568`, same ELF and zero crashes. A same-profile physical
cooperation check remains pending. The user explicitly requested a preserved
commit baseline: implementation `0953c52efec6127e2d933fce361c0f46bf2597d4`
matches the installed package source hashes, recorded in
`verification/harmony-softening/accepted-baseline.json`.
Evidence: `verification/harmony-softening/README.md`.
The two-run microphone allowance remains exhausted.
The pre-listening left phrase and exact C replay are preserved in
`output/harmony-listening-20261014/`.
The post-listening left snapshot is from new power-on Boot ID
`12437594786588333002` and is empty; no crash/dump was reported. The cause of
that power cycle is awaiting the user's clarification, so do not claim a
post-listening snapshot of the same phrase or infer a firmware crash.

The two-attempt consent is exhausted, including across reboots. No further
microphone capture is authorized. Raw PCM was erased after analysis; only local
metrics were exported. Right device is back on accepted ordinary package
`09275b935ad8`, ELF `8c180bbaf1b34e6cf2e2dd1ca961c5a78347ffcbbd125b90a2ef647cc8cd4d5d`,
Current right Boot ID `13916864028309266146`, crash count zero; the left was not
reflashed. The initial restore boot rejected a stable bottom-edge magnetic
field. After the user separated and restarted the right tile, current-boot
logs confirm `magnetometer baseline ready; horizontal contact enabled`.
Restoration and startup calibration are now verified. This is not a new
coupled-playback or acoustic acceptance test.
The ordinary package contains no microphone diagnostic entry point.
See `verification/output-measurement/README.md`; local attempt ledger is
`output/harmony-measurement-20261014/consent-ledger.json` and must not be reset.
Verification and limitations:
`verification/capture-quality/README.md`. Current native build is
`projects/echoy/build-sim-capture-quality`; no new persistent browser preview
was started and all earlier previews were preserved.

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
New neighbors never auto-start, explicit cancellation stays effective, and
language selection updates all three product screens. The cooperation refinement
below now defers new incoming edges during an existing recorded reply.

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

## Two-board baseline (2026-10-12)

The user authorized installing Echoy and subsequent dual-device test versions on
the second board `4553502d49524953010030eda0f40c6c` (initially `/dev/ttyACM1`).
Package `1d57c5719989` replaced its `fruit_slice` application through the official
system-update workflow, operation `33f48138-97df-4354-9a6c-2073e6cded8f`.
HEALTHY boot `2867820668781932022` has the expected ELF identity, unchanged
bootloader and verified Echoy partition layout. The real home screenshot passes;
at 87.5 seconds the same boot has crash count zero and idle audio reports zero
partial writes, write errors and estimated deadline misses. The original board
remains on boot `5127593241566643399` with the identical firmware; it was not
updated or restarted. Both boards still run independently. Second-board touch
and listening, then physical discovery, musical-state transport and playback
clock synchronization remain to be implemented/accepted as applicable.

The second board also reproduced the retained Recovery 0.1.2 startup watchdog
reset before writing. Crash metadata was preserved; no valid Core Dump existed.
The installer waited for a stable ready Recovery boot and submitted one update.
Existing low-level GPIO/I2S startup messages and a display scheduling warning
are retained in the evidence, not classified as new application failures.
See `verification/second-device/README.md` for identities, verification and limits.

## Remaining physical integration

### Approved cooperation revision (2026-10-14, physically accepted)

The user explicitly approved the rules below, revising the old separation
behavior. They are now implemented and verified in the shared C/native simulator;
both authorized devices have been updated. The user subsequently reported that
all seven physical checklist categories passed and requested saving this version.
This report is physical acceptance, distinct from earlier delivery/startup checks.

- A right-side follower with a playable phrase should show only Play in the
  existing middle button area, not a disabled Record beside Play.
- Devices that are not physically joined are independent. Pressing Play on a
  separated device must not wait for an old shared bar, including after Pause.
- Physical separation must end the cooperative relationship and visible
  connected/following state. Retaining a cooperative phrase must not imply
  retaining membership of its former clock group.
- Confirmed additional choice: if joined devices are all paused, the first
  device pressed should start promptly; later entrants align to the common bar.

Also approved: retain the current phrase and existing playing/paused
state when separating, restore independent controls, keep a waiting follower's
Play disabled until usable sound exists, and distinguish confirmed physical
release from radio-only recovery. Preserve the previously accepted deferred
join during recording/one-shot reply and explicit local playback permission.
Frozen HTML remains unchanged. Source baseline `0953c52`, its accepted package
`5d4776c9ae6d` and the speaker profile remain preserved. Both devices now run
cooperation package `2b984e511b98` with `CONFIG_ECHOY_OFFICE_VOICING_TRIAL=y`;
no sound tuning, microphone capture, persistent-data change or remote write
was introduced.

The first Play can come from either side. Validate that a follower can start
an otherwise paused group promptly without leaving the other device on a
different clock, and that clock coordination never grants playback permission
to a paused peer. A solo replay must not wait on a historical group clock.
Validate visible physical detach independently of the next musical commit,
including stale transfers/queued starts and recording's deferred-join boundary.

Pre-implementation review found historical `clock.started`/ownership and a
split follower strip. The owner also derived following from the retained
phrase's source until a later musical commit; that delay is reproduced and
fixed. Confirmed magnetic release immediately removes membership and copies
an independent phase while preserving music/PCM. Radio-only loss instead keeps
physical membership and enters recovery. Host/native tests do not prove the
actual sensor-release latency or identify every layer of the original report.

Physical support currently covers two same-orientation horizontal boards.
The application stores one active peer and ignores a second attach while one
is active; the desktop three-tile model is not a completed three-board physical
implementation. Do not claim arbitrary multi-device placement/order stability.

### Cooperation revision verification and delivery (2026-10-14)

The existing 244x84 middle strip now centers Play on paused followers, including
a disabled waiting state. Native target visibility changes immediately and both
sides of the full area work. A solo replay and an all-paused group's first local
Play use the existing 50 ms lead; later participants align to the common bar.
The private v2 protocol adds a bounded session-scoped CLOCK message with start
acknowledgements and sequence fences. It cannot unpause the peer or retime fixed
ARM/COMMIT boundaries. A first follower start before a recovered clock mapping
is valid is copied/protected, mapped once ready and acknowledged; independent
review found that boundary and its new regression first failed, then passed.
A new radio session cannot expose an abandoned proposal after overwriting an
unconsumed ABORT. Recording/one-shot deferred joins and explicit local permission
remain intact.

Final normal and ASan/UBSan/LeakSanitizer checks pass 26 real two-owner transport/
PCM cases (36,969,333 assertions) and 51 runtime/configuration tests. The existing
24 deferred cases, protocol/linked-owner/band/audio/speaker tests, 96 exact PCM
windows, 43,536 music plans, 864 linked plans/1,552 arrangement transitions,
CTest 2/2 and 12 launcher/HTTP tests pass. GSP 1.5.1 normal/reduced runs pass
50 caption/visibility assertions and eight pointer flows each; the complete
13-group native/PCM regression has zero late blocks/clipping and exact paused
silence. Socket/LeakSanitizer sandbox restrictions were resolved by rerunning
outside ptrace without weakening checks.

All 404 existing scene objects and logical parents are unchanged; the generator
adds only a transparent full-width Play target. Frozen v4 hashes, accepted DSP/
PCM/device-profile sources and the band's entire preparation/voicing/calibration
region remain identical to `0953c52`. The configured pinned-SDK package passes;
its 2,122,368-byte application fits the 13 MiB OTA partition. Only the historical
unused-factory-size warning remains. No new dependency or layout migration.

Both authorized IDs now have package `2b984e511b98`, ELF
`5a721bd70413aabd5716c646b5fd1d29091cffe6464e44c7d994e2923b5d5e95`.
The user freshly confirmed separation >=15 cm on a nonmetal surface before the
writes. Official single-package operations:

- Left `f452a0`: operation `2650a532-634f-4e64-9334-4001b7bb20ca`, HEALTHY boot
  `2026744546928028908`; magnetic baseline ready at 3520 ms.
- Right `f40c6c`: operation `57e470ff-9cee-442f-a4e4-b4b6c70a3666`, HEALTHY boot
  `9361312524343894444`; magnetic baseline ready at 3502 ms.

Identity/HEALTHY/validation, unchanged bootloader and retained partition layout,
actual Record-only homes, silent startup, zero application crashes and zero
partial/write/deadline errors pass. Old Recovery reproduced its known startup
watchdog reset on each board; metadata was preserved (no valid core), stable
Recovery readiness was verified and one upgrade bundle was submitted per board.
That Recovery defect remains unresolved. These installation observations are
delivery/startup checks; the user-reported physical acceptance is recorded below.
Sustained RF/load and measured phase precision remain unaccepted. Evidence,
commands and retained baseline checks:
`verification/cooperation-revision/README.md`.

### Preserved accepted dual-device version and third-board target (2026-10-14)

The user reported "全部通过" for the seven-item physical checklist: waiting/
centered Play; left-first and right-first all-paused starts; playing and paused
physical separation; recording/reply deferred joining and Cancel; and common
updates retaining old sound and paused-peer permission. They explicitly asked
to preserve the current version. This is user-report provenance, not newly
collected agent measurements; individual timings and sensor traces were not
provided. The current physical flow is now accepted within that checklist.

`verification/cooperation-revision/accepted-baseline.json` preserves source
implementation `cca129dc245339d82fe55de0425b1f741ce8248a`, installed package
`2b984e511b98`, ELF/source/UI/configuration hashes and the immutable local bundle.
Local release tag: `echoy-dual-accepted-20261014`. The earlier accepted sound
baseline `0953c52`/package `5d4776c9ae6d` remains intact. Recording acceptance
changes no code, sound, firmware or device state and performs no remote write.
The retained Recovery watchdog, quantified acoustic timing and sustained RF/load
limits remain as previously recorded.

The user plans to connect a third Mosaico and sets a product capacity target of
at most three devices in the same scene. The current desktop arrangement already
has three logical tiles/two edges, but the physical adapter still owns one active
peer per board; its middle device cannot yet act as both upstream receiver and
downstream sender. Replacement third-device identity and baseline installation are verified below.
The user confirmed horizontal-only, same-orientation A—B—C support, including
independent devices and arbitrary pairs. No vertical, L-shaped or rotated scope.
At this preservation point the physical adapter was still dual-only. The
original third-device baseline installation is recorded below; the subsequent
three-device implementation/status is in the replacement section.

Third-device baseline installation: the user explicitly approved reviewed
Recovery 0.1.4 plus accepted Echoy `2b984e511b98` and freshly confirmed separated
nonmetal placement. The ROM probe verified factory MAC `30:ed:a0:f4:0c:d8`;
Recovery subsequently returned verified ID `4553502d49524953010030eda0f40cd8`.
Recovery operation `a03669b4-37ca-4666-a231-adc60b8c232b`, boot
`16758858234958354640`; application operation
`a5593cdd-84f1-471d-85cf-bc2ac56f945d`, HEALTHY/validated boot
`7287316600049295121`, same accepted ELF as the first two devices. Each step
submitted one write. No first/second write, application code/sound change or
three-board implementation occurred. The existing two-device accepted tag/package
remain intact; third-device future upgrade permission is not implied by this
completed two-step installation.

Image/identity, expected partition migration, retained Recovery/bootloader,
real Record-only home and startup audio/crash checks pass at 162.9 s. Inventory
hashes cover the full 0x2000..0x8000 bootloader region; reviewed image plus 144
`0xff` bytes matches pre/post inventory `925d2541ed213aa29f552f15ee6881b899dd72c4dbc81343ae2b4c88caa72cc4`.
A file-only versus region-hash assertion initially failed; the public hashing
implementation and exact bytes resolve it without another firmware write.

**Replaced device f40cd8 (historical unresolved fault):** startup magnetic calibration fails. Both BMM150 sensors
initialize, but three windows report `right_y=-88`, span 2..3, `edges=0x08`
(LEFT, as defined by the pinned header). The initial description calling this
bottom was corrected. Current app boot has zero crashes/output faults but
physical linkage is unavailable. The exact environment versus hardware cause
is not established. After the user confirmed repositioning, one official normal
restart (`d9a0854b-0203-4272-b129-0d1d732831be`) produced boot
`13092980898669071311`. Same accepted ELF, normal home, zero crashes/output faults
pass at 84.0 s, but all three windows again reject LEFT `0x08`, with right-Y now
near 0 rather than -88. The user additionally confirms a bare device with no
external magnetic accessories. Do not keep retrying startup or loosen thresholds.
The pinned classifier's LEFT predicate requires averaged left-X saturation; the
BMM150 fixed-point driver can return -32768 without a register-read error. Actual
left-X and the hardware cause remain unmeasured because the installed application
has no full-axis readback. A local startup-only diagnostic package `2cfd9f7c2bbd`
is prepared/verified but was never installed. The user subsequently replaced
this device; its diagnosis/pending approval must not be transferred to the new
unit. See `verification/magnet-diagnostics/README.md`.
Evidence: `verification/third-device/README.md`; initial ROM-only evidence remains
`verification/third-device-intake.json`. Earlier first/second read-only status
checks confirmed accepted firmware and zero crashes on later boots
`11368868697531345372` / `4144219012607374767`; their reboot cause is not inferred.

### Replacement third-device baseline and horizontal-only design (2026-10-15)

The user replaced former f40cd8. Official discovery and a fresh live handshake
identify `4553502d49524953010030eda0f45936`, factory MAC `30:ed:a0:f4:59:36`,
initially `/dev/ttyACM2`, endpoint `usb:location=1-8.1:1.0`. It initially ran normal
`jump_jump 0.1.0`/ELF `8054b957d334f5517f0044fd2e8ce0f3f68438ef6d319ccb9239f448280238d4`,
boot `1662590448659556080`, crash count zero. Do not reuse the removed device's
cached identity, magnetic fault or pending diagnostic approval.

The user specifically approved replacing jump_jump with accepted Echoy
`2b984e511b98` and freshly confirmed three devices separated >=15 cm on a nonmetal
surface. One official application system update succeeded: operation
`d17dd45f-b043-40a4-a344-f82c3517dafd`, HEALTHY/validated boot
`10218426682111976170`, accepted ELF
`5a721bd70413aabd5716c646b5fd1d29091cffe6464e44c7d994e2923b5d5e95`.
Existing Recovery 0.1.4 was observed on ready boot `15229114980827953342` with
crash count zero and retained; no ROM restoration or bootloader replacement.
Before/after bootloader-region SHA is unchanged
`925d2541ed213aa29f552f15ee6881b899dd72c4dbc81343ae2b4c88caa72cc4`; target partition
sector is accepted `4f8786d12001684a3c02385f31ef167247827bd73b744801494a8c5851bc7231`.

Both BMM150 sensors initialize and startup calibration succeeds at 3483 ms:
right-Y -1, right-Z 205, eight samples, horizontal contact enabled. Real
Record-only home, silent startup, zero crashes/partial writes/write errors/
deadline misses/clipping pass. All three active devices have fresh normal-mode
identity checks with the same accepted ELF and zero crash counts. This resolves
the replacement's hardware onboarding; it does not diagnose the removed f40cd8's
cause or establish three-board cooperation/third-device acoustic acceptance.
Detailed evidence: `verification/replacement-third/README.md`.

The user now explicitly confirms **only a horizontal chain**, with same
orientation as proposed. The concrete design is
`plans/2026-10-15-three-device-horizontal-design.md`: retain per-device musical
owners, add bounded three-node/root coordination, use the pinned public routed
network, and preserve explicit playback permission and detach semantics.
The user approved this design and clarified overlap behavior on 2026-10-15:
first playback remains prompt when an update is locked; its fixed boundary
is retained, the first cycle may be shortened, and unchanged instruments also
align at that boundary. Local owner support now has an explicit v3-only fixed-boundary flag, retained-current
Play while a replacement is pending, exact in-block activation of unchanged banks,
and release when a middle follower becomes the root of its remaining suffix.
The portable v3 coordinator is now integrated with the physical adapter's two
independent seams and a separate official native GSP harness. All 23 normal
command groups and eight ASan/UBSan/LeakSanitizer groups pass, including all six
physical ID orders/both joins, actual independent PCM, original dual regressions
and 51 runtime cases. Review-driven failing regressions cover superseded upstream
exports, cached owner epochs, stale clock ACKs, capture after ARM and bounded
recovery. Official independent native GSP passes in normal/reduced motion (seven groups
each, zero late blocks/clipping and exact paused silence); the ordinary native
reference desk also passes all 13 checks. The pinned SDK build and package layout
inspection pass. Immutable candidate `7c6e37e7be4d`, ELF
`33acbd2b0cbde1b6948af3ce6136f7aa546fd8a1a6938dc4c87d051467ee9016`,
is saved from source `f324cd9` (owner foundation `6ff0f00`). Its 2,125,136-byte
application fits retained `ota_0`; partition table/UI data match accepted bytes,
and no Recovery/bootloader component is packaged. See
`verification/three-device-horizontal/README.md` for exact commands and limits.

After all local gates, the user explicitly approved exact `7c6e37e7be4d` on
replacement f45936 and freshly confirmed all three separated >=15 cm on a
nonmetal surface. One official application update per device succeeded with
HEALTHY/validated and commit-response evidence. New boots, in identity order
f452a0/f40c6c/f45936: `341890509691260521` / `11306869005132987366` /
`3417849056774182729`. All run the candidate ELF, retain their bootloaders,
Recovery and partition/UI components, show real Record-only homes, and pass
current-boot magnetic calibration. At 189.5/119.1/48.3 seconds all app crash,
clipping, partial-write, write-error and deadline-miss counters are zero; startup
PCM remains silent. Detailed operations/traces/screens:
`verification/three-device-horizontal/device-results.json`.

Original Recovery 0.1.2 startup watchdog resets recurred before the two stable
ready boots; metadata is preserved, no valid Core Dump. New Echoy boots have zero
crashes; replacement Recovery 0.1.4 had zero crashes. This retained issue is not
fixed by the app. Three-board joined interaction/listening, measured acoustic
phase/drift and sustained RF/load remain unaccepted; use the physical checklist
in the verification directory. The saved accepted dual tag/package and sound
remain intact. Completed replacement installation is not blanket future write
permission. Old-target diagnostic `2cfd9f7c2bbd` remains archived and uninstalled.

#### Right-attachment timeout fix (2026-10-15)

The first v3 package's three-board physical acceptance remains open: the user
reports stable recovery on attaching a right tile. Live identity/owner evidence
confirms the expected apps with zero crashes. Root cause: START's absolute root
deadline was compared with local uptime before clock mapping, using zero offset.
Physical boot differences are roughly 70 seconds; a follower running ahead by
more than the 30-second budget immediately aborts. The old 0/200/400 ms harness
missed this. Both the two-device and three-device regressions fail on old code.

Fix `a83a64e` keeps a bounded local deadline until mapping, then uses the original
translated root deadline. Superseding edits cannot extend the same budget.
No timeout/retry, wire, UI or accepted sound change. Tests cover all six
0/70/140-second clock assignments, steady pairs, both triple joins and first
starts, plus lost mapping/ABORT with edits. All 23 normal/eight sanitizer groups,
51 runtime cases and both independent native GSP modes pass.

After explicit exact third-package approval and fresh separated nonmetal
placement, all three received one official `ba951963754a` update. Expected ELF
`cc151795ec3efd59c639ce09b613f480ca601660f14a60cb1acd2639a6e1b711`;
new boots f452a0/f40c6c/f45936: `9463761514545310676` /
`17925284075629591042` / `12347754192822218685`. HEALTHY/validated, retained
components, real homes, current-boot magnetic readiness and zero app-crash/output
errors pass. The old Recovery startup watchdog remains separately recorded.
Evidence: `verification/three-device-clock-start/README.md`. The user repeats the
original failing pair/order and confirms "显示跟随左侧，保持稳定". Agent readback
verifies f45936 left/f452a0 right, about 180 seconds of boot offset, a committed
right phrase and real "跟随左侧" home 46.776 seconds after current-boot contact.
Both remain locally paused; f40c6c is solo. At 341.0/243.6/161.0 seconds all three
same new boots still have zero app crashes/output faults. The reported right
attachment defect is confirmed resolved within this pair/order. Keep the accepted
dual/sound baseline; full three-device checklist/listening, precise acoustic
timing and sustained RF/load remain unaccepted. Physical evidence:
`verification/three-device-clock-start/physical-regression.json`.

The user subsequently reports "三台也能正常工作" on this installed fix package.
Record this as user-confirmed basic three-device operation, not newly measured
agent evidence or acceptance of every checklist category. Join-order changes,
independent first/later starts and Pause, fixed updates, detach permutations,
deferred recording/Cancel and continuous-use checks remain individually pending.

The next live ABC→BCA swap fails: the user confirms left-to-right Harmony
(B=f452a0), Voice (C=f40c6c), Pulse (A=f45936). Read-only initial/later owner
snapshots show B CONNECTED=1, C FOLLOWING=4 and A SOLO=0; all idle, no pending or
failed preparation, expected unchanged ELF/Boot IDs and zero crashes. A should
follow C in this layout. Record checklist item 3 as failed for this transition;
do not generalize the initial basic-three success to live rearrangements.
Evidence: `verification/three-device-reorder/README.md`. Root cause remains
unresolved (contact/radio admission/old membership not yet separated). This
observation made no production-code edit, restart or firmware write; retain
the current failed BCA scene for diagnosis.

Repair investigation: reattaching only the right A with B—C unchanged still
fails by user report. Logs capture C's RIGHT release/contact observations, while
A's current firmware has no live sensor/filter/contact readback. The real-owner
portable ABC→BCA transition succeeds with substituted physical/radio adapters;
it does not resolve the hardware-path cause. A default-off, link-worker-only
bounded diagnostic is saved in `bec0ffb`: existing compensated samples plus
filter/contact/peer/group state, at most one valid record/second, ninety total.
No threshold, retry, wire, music or UI changes. Unit/sanitizer, adapter, full
three-owner and 51 runtime checks pass; native GSP artifacts are byte-identical
to prior verified normal/reduced evidence. Exact diagnostic `1eb36e7d795a` for
f45936 only is built/inspected and not yet installed, with known `ba951963754a`
restore package retained. The user explicitly approves the diagnostic/restore
and subsequent Echoy firmware updates on all three active devices without
per-package permission requests. This standing authorization is now in AGENTS.md;
older per-package-only third-device permission notes are superseded. Actual
separated nonmetal placement still needs confirmation before restarting handled
devices. Cause/fix remain unresolved; see
`verification/three-device-reorder/diagnostic/README.md`.

#### Shared Flash thresholds (user-approved startup change)

The user explicitly rejects per-device volatile baseline capture and requests a
common threshold set stored in Flash. `6542bd7` uses the pinned readonly
`MOSAICO_MAG_CALIBRATION_S31_V1` table; ELF confirms its 44-byte `.flash.rodata`
object. Horizontal predicates do not use the vertical Y/Z reference, so the app
initializes a fixed zero reference and masks vertical results instead of capturing
a startup baseline. The thresholds, eight-sample filter, three-frame confirmation
and failed-read release are unchanged. No NVS calibration dataset or per-device
threshold override is created. This supersedes the separated-startup requirement
for new horizontal-only builds; legacy rollback artifacts retain their old rules.

The real-BSP classifier/active-adapter regression fails on old already-joined
startup and passes all four contact masks after the change. All 24 host groups,
related sanitizers, 51 runtime cases and a fresh official native GSP run pass.
Normal `0baaaabeba0d` (ELF `f8978cc68243fc845299281b69bee1903cfe9e3f2ca08ed3698080dece9056bd`)
is installed/HEALTHY on f452a0 and f40c6c; boots `1589398709091682315` and
`14085768804998349107`. A/f45936 temporarily runs matching diagnostic
`e793bf639647` (ELF `f008d365f4411297c84209b8534fd6a7a6d230280f3bb94af9ec4440fd76725c`),
HEALTHY boot `18031833374556782289`. All three show fixed-Flash sampling readiness,
no baseline capture/rejection and zero startup app/output faults. Retained
components, UI and accepted office sound pass identity checks. Old diagnostic
`1eb36e7d795a` was never installed and is superseded.

The user has now confirmed B—C—A. An official joined restart of diagnostic A
(boot 2940862482323986586) yields 55 valid samples: left X/Y=-32768 and
filtered Z=620..623, but no reported left contact. The pinned Z<650 rejection
causes the missing physical edge. A shared application-owned Flash profile now
sets only left_reject_z=600; the pinned false-left capture Z=560 stays rejected.
No per-device settings or startup baseline are used. Recorded-seam regression
fails before and passes after, together with 25 pinned capture projections,
boundary/filter/release checks, related sanitizers and full three-owner/actual
PCM permutations. Native application sources remain identical to previously
verified normal/reduced simulator results. Accepted sound/UI are unchanged.

Normal package `f9b87dd4d4d7` (ELF
`aae48ef4dcabf0cb9a87c6a07017f935f7212d23d429a8ff13881f52d5fddb89`)
is now installed/HEALTHY on all three; A is no longer on diagnostics. Source
correction is `013b778`. Exact image/UI/retained components and shared Flash
startup logs pass. No per-package permission or startup separation is required.

The user confirms both C and A follow in B—C—A. Read-only snapshots agree:
B cooperation=1, C/A cooperation=4/following=true, committed playable banks,
all paused, no pending/failed preparation or app crashes. This is eventual BCA
success after intervening contact release/rejoin events, not clean startup
acceptance. Initial joined rollout exposed same-edge 180-degree BSP sessions
that Echoy rejects; a probe with actual unmodified BSP session/topology code
reproduces the mismatch. That additional negotiation gap remains unresolved.
B/C also log transient ESPNOW NO_MEM sends before convergence; sustained RF
health is unverified. A has an intervening boot change with zero crashes.

The user asks to restart testing from startup and confirms all three separated.
Official planned software restarts and independent-start checks pass: boots
16946015599629738310 / 14630519438785164093 / 14949665561685451233,
all SOLO/empty/paused, no contact events, baseline capture, output errors or
crashes. Physical power cycles were not performed. Independent A recording, A—B with C separate, and A—B—C silent following
then pass user checks plus read-only owner verification on these same boots.
Test 5 failed by user report: fast A movement from ABC left to BC right was
not recognized; removing, pausing and rejoining A showed connected. Current
same-boot readback: B cooperation=1, C/A cooperation=5/following=true (recovery),
A requested revision14/committed12, all retain phrases and are paused with no
app crash or preparation fault. Sustained official live logs show repeated
ESPNOW NO_MEM sends, concentrated on C; this alone does not establish whether
heap pressure or transmit backlog is responsible. Preserve current B—C—A.

The default-off 1 Hz/90-sample diagnostic was extended with existing group
identity/attempt/members/offset/deadline. Initial21bc020ef620 installed only on B;
Iris split its long lines, so revised90f5098a2b4f uses a separate short group
record and passes completeness checks. It too installed only on B. B's complete
readback confirms group1/rank0/attempt0/no admitted peer after a same-edge SDK
session. C/A were never flashed with these diagnostic candidates.

A throwaway complementary-edge-only restriction in the actual SDK session probe
passes five static orderings but leaves one pairing the two endpoints across
the middle. Direction filtering alone is insufficient. Further diagnostic rollout
was stopped. B is restored to normal f9b87dd4d4d7, HEALTHY boot
13772919324190082093, operation a4912e78-a132-4dce-b1e5-d73e39205936;
image/sampling/no-output-error/retained-component checks pass. C/A retain normal
f9b87dd4d4d7 and software-restart boots14630519438785164093 /
14949665561685451233. No temporary diagnostic remains. Test5 remains failed;
no functional session/transport fix has been applied.

The user approved the architecture/compatibility repair (“同意推进”). The
application-owned `echoy_radio` adapter is implemented over public peer-link
APIs: full contact/boot/generation scene advertisements, challenged boots,
reciprocal unique-chain admission, bounded addressed/one-hop transport and
first-wire clock timestamps. Generic SDK auto-pairing is no longer used. The
same radio network ID observes legacy claims, but only ECHN/version1 participants
can admit a graph. Known stale participants are not silently dropped. Sound,
UI, Flash600 thresholds, group musical semantics and Recovery remain unchanged.

Local25 host groups (including51 runtime cases) and related sanitizers pass.
Actual group/band/PCM owners run the new transport for all six orders, immediate
root-to-tail reorder and detach. Independent review closed a reproduced old
advertisement replay after RX recovery by separating freshness from persistent
sequence/generation watermarks. Native testing exposed a queued-send clock bias;
first accepted raw submission now stamps matching PING/PONG fields, with immutable
retry/forward bytes. Known-offset tests pass without relaxing the10ms limit.

Package `9d00b69f4fce` (ELF cd0487be492dee5fa62087360b2df488c59581004d4e6f3debd659405806fe6c)
from implementation commit69f64a1 is installed/HEALTHY on all three. Boots:
B16824075800820969890, C17232752916447458424, A9167656871751075119.
Identity, retained components, fixed sampling and startup audio checks pass.
Normal/reduced official native checks had each passed seven checks with zero
PCM misses/clipping/silent paused output and unchanged10ms clock comparison.

Hardware admission is now fresh/three-member and A explicitly selects C on its
LEFT, but stable transport remains failed: B/C still emit ESPNOW_NO_MEM (C has
1174 cumulative raw submit failures and206 relay/ACK drops in the captured window).
Empty owner states B/C/A were1/5/2; no user fast-reorder acceptance is claimed.

The TX-completion follow-up5bfaff7 is installed on all three as
0606e70d3f3c (ELF c7e6f0cb031d7c0e496938386e7261958668988271f425b527e3316d360b28b6).
The public TX callback gates submissions using static atomics; waiting consumes
neither retries nor clock stamps. Application ACKs remain required. Final25
host groups, sanitizers, fixed SDK build and matching native normal7/7 pass,
including original clock tolerance, zero PCM misses/clipping and paused silence.
Independent callback/lifecycle review is clear.

Current boots: B13347265088592221265, C15872866450408188654,
A8281715504648553660. Identity, HEALTHY/commit, retained components, fixed Flash
sampling and startup audio checks pass. Actual B—C—A graph agrees across all
three (fc2f3efc2f779672), with correct reciprocal neighbors. Empty owners are
1/2/2 (root connected, followers waiting), not recovering. Captured unified raw
submit-failure counters remain0; app retries and relay/ACK drops still occur.
C has7 cumulative MAC failures across rolling updates. No application crashes,
but zero-loss/sustained-RF or acoustic timing acceptance is not claimed.

The user reported B/C following silently after recording in A—B—C, but later
same-boot official readback found both recovering (cooperation5), and C had
preparation_failed. This is not a stable pass. All three retained correct graph
4ab35f2f5fa87420 with zero raw submit failures. A's captured source is seed6768,
six touches; B Harmony and C Voice selection order is unknown to the user.
Both selection orders must work. Read-only plans and failure evidence are in
output/group-commit-diagnostic-20261015/ and
output/horizontal-tx-completion-20261015/abc-user-confirmed/.

A follow-up fixes two reproducible commit-pressure failures: ACK/relay queues
now rotate their individual slots so reused low slots cannot starve older work;
three-node ARM preparation reserves the existing one-second budget per route
hop (two seconds), while pairs retain one second. This selects a future bar
before ARM; locked bars, first Play,200ms guard,30s transaction limit, retries
and sound remain unchanged. Passive first-failure snapshots retain the original
reason/attempt/clock/owner state before ABORT for later readback.

Radio starvation regressions and real radio/group/band/PCM tests using seed6768,
both role-selection orders,20ms simulated MAC completion and1.8s preparation
fail before these changes and pass afterward. These are stress profiles, not
measured live RF latency. Full26 host groups, radio/pressure/adapter sanitizers, SDK build and matching
native normal/reduced7/7 each pass, including unchanged10ms clock checks and
zero PCM late blocks/clipping/exact paused silence. Verified candidate548906cbbae4
retains accepted sound/UI/Recovery and is installed/HEALTHY on all three.
Current boots B3705190959680047732, C10474193707046465341, A1371895758714733862.
Official commit/identity/retained layout/fixed sampling/startup audio checks pass.
B/C old Recovery each reset before stable readiness and the single write;
no valid core dump, application crash counts remain0. Physical acceptance is
still pending; see verification/horizontal-commit-fix/delivery.json.
Keep physical A—B—C for the next recording check, then original fast ABC→BCA.
Test5, further permutations, detach retention and joined-start remain open.
No further installation permission or startup calibration question is required.
Evidence: verification/horizontal-commit-fix/ and
output/horizontal-commit-fix-20261015/.

Latest live observation: all three agree on ABC graph7ecbaf627c317712,
group7e214f02/attempt2/phase5, correct ranks0/1/2 and neighbors. Raw submit
failures remain0 and audio error/miss/clipping counters0. However all retain
RADIO fault2 for the same graph's previous attempt1 after rollout; this is not
solely an old-boot graph fault. B has13 cumulative queue drops and10 MAC failures;
no zero-loss/stable physical pass is claimed. Attempt2 has completed. Evidence:
installed-abc-observation.json and output live logs. The user is now asked to
set middle B Harmony/right C Voice before recording on left A, then hold35s
after the one-shot; official180s log capture is active.
Next answer must be checked against current snapshots/logs before rapid ABC→BCA.

### Dual-device basic physical flow accepted (2026-10-12)

Physical linking is now implemented locally for two upright, horizontally joined
boards: magnetic contact confirmation, full-phrase wireless transport and mapped
bar clocks feed the existing shared C arrangement/audio owner. Each board keeps
explicit Play permission; detach retains the exact committed phrase. Radio/RF
startup finishes before audio activation. Existing UI and output voicing are
unchanged. The pinned BSP's network/interaction/peer components are used, with
the existing battery driver explicitly retained at 0.1.2.

The user confirmed separated placement. The previously accepted package `2822d40910b0`, ELF
`8cf4eecdfb15e6dd73cd5a7b9a6b84fc355d0a128927be5c7e9cb813104a554f`,
was installed on both authorized Device IDs. Immutable package:
`output/device-release/echoy-dual-active-2822d40910b0.irisfw`.
First board HEALTHY boot `3411308831530673249`, operation
`2c440ef6-d3ef-4dc2-ac66-85aeadace29d`; second board HEALTHY boot
`17407228737210624198`, operation `57201513-205f-43f7-97b8-a8cc6558596e`.
Both real homes and separated magnetic baselines pass. At 202.7/97 seconds the
same boots have zero application crashes, partial writes, write errors and
estimated audio deadline misses. Preserve Recovery; installation authorization
already covers both devices and does not need to be requested again.

Initial package `7951d5c491a0` was rejected on the first board after the first
silent audio block exceeded its deadline. Diagnostics found about 36 ms of
repeated empty cue-slot scanning; internal-memory placement was already correct,
and core affinity alone did not solve it. Active-slot gathering now preserves
bit-identical PCM while reducing measured idle processing maxima to 6,818/6,836 us.
Audio remains on core 1 with bounded diagnostics; block duration, deadline limits
and sound parameters are unchanged. Both boards have about 10 KiB internal RAM
free at idle; joined operation later reached about 3.2 KiB in one left-board
sample, so long-running memory headroom still needs evaluation.

Actual RIGHT/LEFT contact sessions and complete 849-byte phrase transfers are
now verified between the first board on the left (Voice) and second on the right
(Harmony, including Broken). Both emitted nonzero PCM with zero output errors,
partial writes, deadline misses and application crashes. Active processing maxima
were 14,901/9,610 us; keep these distinct from the 6.8 ms idle observations.
The user confirmed that the pair sounded in time without obvious problems, and
that pausing the left did not stop the right and detaching retained the right's
phrase/playback. Logs also show continued right-side output after detach, followed
by the user's pause; final screens retain both phrases. Do not repeat these
accepted basic steps just to recover context.

The user noted the missing persistent "跟随" text during that acceptance. The
cooperation refinement below now implements the explicitly approved status
feedback; those earlier observations describe the previous firmware.
Earlier metadata timeout/reconnection events were observed; their cause is not
established. Clock estimate RTTs were 13.5–73.9 ms, without a measured acoustic
offset/drift bound. Long-duration RF/load robustness remains open. Evidence and
user-report provenance: `verification/dual-device/physical-acceptance.json`.

Local verification passes: 96 bit-identical PCM windows against the frozen old
mixer (including explicitly audible windows), codec 11,236 round trips/232 rejected inputs, host
transport/clock tests, 23 runtime cases, existing band/calibration tests, 864
plans/1,552 transitions against the frozen reference, CTest 2/2, and 13 native GSP
desk checks with real PCM and zero late blocks/clipping. Relevant sanitizer runs
also pass outside ptrace. The final pinned-SDK bundle build passes with the
expected factory-partition size warning (the package targets the much larger
`ota_0` partition and preserves Recovery). Basic actual radio transfer/contact
and listening now pass within the scope above; precision timing and sustained
memory/radio load are not accepted yet. Only two upright horizontal boards are
supported. That accepted v1 firmware propagated edits after transfer/preparation;
the v2 refinement below implements shared future-bar transactions. See
`verification/dual-device/README.md` for
implementation, artifact identities, test commands and acceptance boundaries.

### Cooperation and audio-publication fixes installed (2026-10-13)

The four approved decisions are implemented against unchanged v4 HTML. Contact
acknowledges immediately but defers joining through an existing count-in,
recording and one-shot reply. Cancel/Pause and detach/rejoin remain independent.
Wireless v2 prepares both sound banks before a common future bar; failures keep
committed sound and no connection/commit grants Play permission. Detach retains
the collaborative phrase; no restore-own entry or reboot memory was added.
The existing status line shows connected, waiting, deferred, following and recovery.

The earlier right-side apparent restart was an audio error page with unchanged
Boot ID. A four-round diagnostic trace measured `view_lock` publication waiting
16,806 us and missing PCM time by 1,841 us. Publication now uses a zero-wait lock
attempt and retries next block; UI/radio readers cannot block the PCM owner.
`cache_busy` telemetry counts deferred publications. No audio deadline, DMA lead,
recording timing, volume or sound parameters were relaxed.

Recovery now acknowledges pre-OFFER ABORT without repeatedly resubmitting it;
held source candidates cannot escape a failed transaction if ABORT overtakes
BEGIN. Existing WANT messages resume after owner readiness or fresh peer traffic
following actual silence. Every attempt remains 30 seconds; duplicate messages,
continuous heartbeats and failed baseline markers do not trigger retry loops.
Wire version/layout remain v2. Deferred local jobs, already-authorized commits
and explicit playback permission remain protected.

All 26 runtime tests pass, including two 80 ms cache-reader contention regressions
that fail on old code. The 18 real transport/PCM cases (33,039,014 assertions),
24 deferred/owner cases, protocol/calibration checks and relevant ASan/UBSan
runs pass. Fresh native GSP verification passes 6 flows and 46 caption/visibility
assertions; frozen reference hashes are unchanged. Evidence and commands:
`verification/cooperation/README.md` and `publish-lock-trace.json` there.

Package `f13fd6428d8b` was installed on both authorized devices, ELF
`f57805292875250767cf6f76a111a6d6a3914775438b5d90cd791768ef651420`.
First installed HEALTHY boot: `7368647184006517658`, operation
`1d41206d-1d5c-461e-8ebc-a7afd7abf3a7`. Second installed HEALTHY boot:
`14992168401857743263`, operation `58219d2c-cc92-4374-aeaf-7b8d6bafa456`.
Identity, retained bootloader/layout, actual homes and magnetic baselines pass.
The right's old firmware could not allocate the software recovery-entry task;
user-assisted AI-button entry resolved delivery without rewriting base firmware.
Recovery boot `15016243081138835729` had crash count zero. The two prior software
attempts submitted no update bundle; the manual-entry path submitted one update.

The user confirmed the repaired first Role click and initial full phrase
transfer to a silent follower, then reported no exceptions during the requested
playing/rejoin/shared-update checks. They did report slower phrase transfer and
selection feedback, plus pre-existing Harmony distortion; these remain open.
The latest captured boots are `2646500753984770485` / `4969769315098002880`, both
power-on reset reason 1 with crash count zero and the same f13 ELF. Do not label
the intervening user power cycles as crashes. Live audio telemetry has no
deadline misses, partial writes or write errors; nonblocking cache publication
has been exercised. Physical RF/load or acoustic precision beyond these targeted
checks is not yet established.

### Switching latency and internal memory (2026-10-13, targeted hardware checks passed)

The implementation is committed locally as `443dbfa`. It preserves the frozen
HTML, sound/output parameters, recording timing, explicit playback permission
and common-bar readiness/authorization rules.

- UI selection feedback precedes image encoding; the three unchanged gallery
  images are submitted across callbacks, center first. Continuous hover/drag
  cannot postpone them. External state is read once per timer callback.
- Physical followers omit synthesis/calibration of the inaudible upstream
  replica while retaining full plans/clocks; desktop A/B/C still have real banks.
- Transport has two data slots plus one priority control slot within the BSP's
  eight slots, with unchanged v2 frames, complete coverage/CRC/decode and deadlines.
  Retired transfers retain their slots until released. WANT retries the current
  transaction serially for older v2 receivers; new transactions restore two.
- Retained encoded images and the large UI view cache use explicit PSRAM storage.
  Double-callback external UI skips its unused standalone model; zero/single
  callback behavior remains. Band models and global malloc policy are unchanged.
  Startup allocation diagnostics and concrete ESPNOW error names are available.

The f13 baseline was 3.572 s from proposal to common bar: 1.630 s for 693 bytes,
0.601 s until both ready, then 1.341 s waiting for the scheduled bar. Gallery work
cost 151,873 us; UI callback/gap maxima were 165,764/357,823 us. The user's earlier
15 s was an observation window, not a measured requirement or acceptable target.

The first optimized package exposed internal-memory pressure: gallery COPY
sources retained 7,499 bytes while hidden, and software Recovery entry failed
with `ESP_ERR_NO_MEM`. c4 moved those images but still left one boot with 2,315
internal bytes; a default-home join then produced explicit ESPNOW NO_MEM.
The UI-only package `150bd97add2b` also removes a redundant 14,256-byte
model and places the 14,764-byte cache in PSRAM. Its ELF is
`09644dd9013d660b1237b2ae016f6d4f3637a99313d8e8988d5f2e7c91b2163d`.
Prior failures, exact artifacts and timing limits are in
`verification/cooperation/README.md`.

Right-only installation succeeded: operation
`5187cab1-eb40-4491-abdd-7bbfb58eb9b0`, initial HEALTHY boot
`12837437181173456188`, Recovery boot `2790096483567616551` with zero crashes.
Image, retained bootloader/layout, actual home and separated magnetic baseline
pass. Internal free rose to 27,031 bytes at home and remained about 26,700 bytes after
right gallery navigation, phrase reception and repeated expression updates.
The skipped UI model and external UI cache are confirmed; all three band models
remain internal. No NO_MEM, audio deadline misses, partial writes or write errors
appeared in the captured regression. Right PCM remained zero during silent follow.

The user confirmed the original right-gallery -> left-record -> join path now
receives a phrase, shows following and stays silent. They also report expression
selection and phrase updating both feel smoother. Subsequent completed transfers
of 588-771 bytes took 0.540-0.649 s; complete proposal-to-bar intervals were
2.246-3.706 s. These differ from the old 693-byte plan, so do not claim a controlled
percentage improvement or touch latency. Initial join took 4.355-7.755 s from proposal to bar across the two observed
runs, including session/clock setup and bar waiting; startup latency remains an
optimization item.

Both devices finished that round on `150bd97add2b` (superseded by the quality
package above). Left software Recovery entry and update
succeeded without manual intervention: operation
`12f303ba-45d5-42e9-a91f-0ec896d71907`, HEALTHY boot `1399056586306074643`.
Right current boot is `15501297783550630593` after an intervening power-on reset
(reset reason 1, zero crashes). Both image/retained-layout/home/calibration checks
pass. The user confirmed the final same-version test: silent initial follow,
continued playing through common updates, smoother switching and right Pause
remaining effective, with no stop/reboot/obvious misalignment reported.

Final logs retain those Boot IDs with zero crashes, NO_MEM, audio deadline misses,
partial writes and write errors. Right transactions 3/4 committed while paused.
Internal free after the exercises is 26,603 bytes left and 31,047 right. Both
finish paused and linked; preserve their current phrases. Same-version expression
transfers took 0.410-0.660 s for 527-771 bytes, with complete proposal-to-bar
intervals of 3.414-4.066 s. Those intervals include the unchanged musical bar wait.
Final evidence: `verification/cooperation/ui-cold-final-acceptance.json`.

Local validation passes: 16 UI construction, seven scheduling, ten image ownership
and twelve encoding cases with sanitizers; 27 runtime cases; 18 real transport/PCM
and 24 deferred cases; 96 bit-identical PCM windows. Native image comparisons and
six current three-tile cooperation flows pass, with zero late blocks. The pinned
SDK package preserves UI data and retained layout, with only the known irrelevant
factory-size warning (the package targets OTA). Hardware timing/acoustic precision
and sustained RF/load acceptance beyond these targeted observations remain open.

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
That retention decision was superseded by the user's explicit all-role loudness
request on 2026-10-14, described above. It was provisional retention, not a claim of complete acoustic-quality
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
resource watermarks and sustained RF/clock behavior on hardware, retaining the
accepted basic two-board flow above. Three-board hardware support
still needs implementation/acceptance. Device
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
distortion at the same higher volume. Physical listening follow-up was closed
for that trial. On 2026-10-10 the user decided to retain the current volume
("音量上就先这样吧"). The current packages retain the common output parameters from
`7c431dacfd59`, with the additional left-only Harmony trial described above.
The 2026-10-14 all-role loudness request explicitly reopens tuning. This was provisional retention,
not a claim of measured acoustic performance or the absolute hardware limit.
Any future substantial loudness improvement needs acoustic/output-chain
measurement or a separately evaluated spectral adjustment.
On 2026-10-13 the user again reported Harmony distortion with the same retained
profile, explicitly predating the cooperation changes. All three expressions
distort progressively with increased volume; Answer is somewhat better.
After the user returned home without moving the slider, the right screen showed
Harmony Arpeggio, following and playing, with knob center around y211: roughly
34% volume from `(249-211)/111`. The user confirmed distortion also occurs during
unchanged continuous playback, not only at a switch/start transient.
This is a screenshot estimate and listening
report, not an exact digital setting, SPL measurement or identified component
failure. Preserve this starting point for a separate sound investigation; the
latency changes do not retune gain, limiting or timbre. Current quality acceptance
is open despite the earlier trial's positive feedback.
The unchanged PCM processor's telemetry has a confirmed interpretation limit:
after one transient, floating-point high-pass tails and an envelope that settles
at `0.999971389771` can increase `limited` by 720,000 during 30 seconds of entirely
zero integer output. `clipped` checks only post-limiter values beyond +/-1, so
zero clipped samples does not establish absence of upstream/limiter/acoustic
distortion. An offline representative `40aa`/.5 test at .34 volume still reaches
the .60 limiter for all Harmony expressions; it is not the exact current failed
plan. Do not attribute this issue to a physical component from those counters.
No audio/telemetry code was changed. Next sound investigation should first
capture/replay the exact failed plan and compare bank, high-pass/drive and limiter
stages on effective audible samples rather than cumulative counts.
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
