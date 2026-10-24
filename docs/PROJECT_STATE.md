# Echoy project state

## Iris 0.2 migration candidate

This branch prepares version 1.2.1 against a pinned matching Iris/Vibe Mode 0.2
workspace, preserving the accepted v1.2.0 application behavior. The candidate
build, offline package checks, C/PCM regressions and native three-owner checks
pass. The 1.2.1 candidate was saved as a Mosaico platform draft; the user has
since reported that platform review rejected it for startup/memory, rendering
and LittleFS failures.

Authorized single-device diagnosis on 2026-10-22 reproduced the startup failure.
Initial main-task brightness control raced the first display transfer. After
moving it before rendering, radio initialization exposed internal DMA allocation
failure (`ESP_ERR_NO_MEM`). Display scratch now uses one PSRAM DMA buffer. The
host's post-HEALTHY Flash inventory also exposed an audio deadline failure;
I2S interrupts are now IRAM-safe, with a compile guard and bundle validation.
Audio buffers, deadlines, musical synthesis and UI behavior are unchanged.

The rebuilt 1.2.1 candidate passed the official installation/healthy check on
one device with the reviewed Vibe Mode 0.2.0 base. Six startup tests, 55
device-runtime tests and the board-audio checks pass. The original faulty
archive remains preserved. The fixed candidate and validation boundaries are
listed in [migration details](iris-0.2-migration.md). Final physical interaction,
listening and multi-device acceptance remain separate from startup acceptance.

The user subsequently reported continuous vibration. Echoy had no motor
initialization despite having no haptic feature. It now explicitly disables the
motor before other startup work; failure keeps Iris available and stops the
application. The replacement candidate passed healthy installation, and the
user confirmed that continuous vibration stopped after returning to Echoy.

Recording and repeated role changes then exposed an audio deadline fault,
not a chip reset: Boot ID stayed unchanged and no core dump was present.
Diagnostic DMA completion intervals remained approximately 5 ms while completed
buffers overflowed their queue. The PCM owner now runs at priority 6, above
the pinned Iris/TinyUSB tasks at 5, with build-time checks against Iris/GSP
priority changes. The new task-creation regression fails on the old setting.
The final package excludes timing probes and passed healthy installation.
In the single-device recheck, recording/playback and 20 subsequent plan updates
completed with zero write errors or deadline misses; the user reported no
protection page in this run. Long-duration and three-device acceptance remain
pending. The original strict fault handling is unchanged.

A later power-on test exposed LCD transfer `ESP_ERR_NO_MEM` even while the
rendered shadow and application state appeared normal. The pinned BSP's internal
DMA staging used a ten-entry LCD queue. An application-scoped wrapper of the
public panel-IO constructor now caps only the board's QSPI LCD queue at two,
preserving the BSP transport flags and bounding staging payload to roughly
16 KiB plus allocator/descriptor overhead. Host policy and archive-link tests
pass; the actual ELF calls the wrapper. Five distinct software restarts showed
normal application state and no observed LCD transfer errors. The user confirmed
two physical power-on starts reached a responsive home screen; the final cold
boot was also checked through live identity and transfer logs. This replaces
the earlier insufficient installation/shadow-only startup conclusion.
The subsequent startup optimization moves only fixed timing/tap cues and the
four audition phrases to build-time generation using the original C synthesizer.
Float32 samples, eight onset choices per role and the selected office gains are
preserved; recorded music still uses background synthesis. The existing startup
UI is unchanged. On the tested device, sound preparation fell from approximately
3.42 seconds to 0.132–0.148 seconds across the final candidate's installation,
two software restarts and final cold boot. This measures sound preparation, not
total power-on time.
The app image grows by 1,000,192 bytes; the PSRAM rodata copy adds about 1 MB of
steady memory use, leaving approximately 10.2 MB free in the observed idle state.

Three host profiles match original synthesis sample-for-sample, including all
audition bounds. Allocation/corruption tests, sanitizer checks, the existing
55 runtime/6 startup tests and ten native simulator checks pass. A separate ESP
diagnostic compared all seven assets with on-device synthesis: maximum absolute
sample error was 1.45e-6 and relative L2 error at most 1.63e-6. The installed final
candidate excludes that diagnostic and has normal application state, no observed
LCD transfer errors and zero audio deadline/write errors in the startup checks.
The baseline's existing GPIO-service/already-disabled-I2S initialization messages
remain; they were not introduced by this optimization. The user confirmed two
physical power-on starts, audition of all four roles, recording and repeated role
changes were normal on this candidate. The final cold-boot readback also showed
normal state and zero audio write/deadline errors after those interactions.
Long-duration and three-device physical
acceptance remain outside this single-device check.

Two follow-on preparation optimizations are now installed on the same device: foreground/FULL
share each anchor note's synthesis while retaining independent effects and
normalization, and the build generates 36 shared triangle tables for common
chord pitches. The public MIDI 0–127 range retains runtime construction outside
the cached range. Tables add 589,824 bytes of immutable data; the shared rendering
uses existing SOURCE storage temporarily instead of allocating another loop.
Against the retained `eff8303` source, 224 host plans have bit-identical samples
and normalization before/after office voicing. The duplicate-note regression,
table comparisons, ASan/UBSan, 55 runtime/6 startup tests and ten native checks
pass. LeakSanitizer was disabled for the sandboxed sanitizer run.

A silent twelve-plan same-device diagnostic compared identical seeds/density
before and after the change. Across three expressions, mean preparation times
were 1.060 to 0.556 seconds for Harmony, 1.073 to 0.929 for Voice and 0.361 to
0.324 for Dust; Pulse was approximately unchanged (0.393 to 0.381). Maximum
sample error was 2.50e-7 and relative L2 error 1.26e-6, within the existing check.
These are paired startup-worker measurements with UI active, not a general
speed guarantee. The ordinary package excludes both reference synthesis and
diagnostic code, and passed two software restarts with normal application state,
zero audio write/deadline errors and no observed LCD transfer failures. Idle free
PSRAM was approximately 9.63 MB. The user accepted the current single-device
experience on this package. This records user acceptance without claiming an
additional instrumented cold-boot run. Long-duration and three-device physical
acceptance remain pending; the earlier fixed-audio package remains preserved.

The tested device's NAND/LittleFS mount failure was repaired on 2026-10-24
through an explicitly authorized filesystem rebuild. A target-bound temporary
maintenance image formatted the NAND block device, wrote and read a 16 KiB
probe, then verified it after a real reboot and removed it. The empty-filesystem
check passed. The unchanged Vibe Mode 0.2.0 image then registered its NAND file
service with no mount error in the captured boot. The exact current Echoy
package was restored healthy; bootloader and partition-table hashes matched
their pre-maintenance values. The production application contains no formatter.
This repairs that device's filesystem state, not other devices automatically;
full media health and power-loss testing were not performed.
Private device identities, logs, screenshots and the scoped ongoing device
authorization are retained in ignored local evidence, outside release content.
See [migration details](iris-0.2-migration.md) for the changed contracts and the
device-transition procedure. Further device work must use its applicable task
authorization.

## v1.2.0 source

The accepted phrase pickup, complete battery percentage and richness feedback
fixes are frozen for v1.2.0, retaining the v1.1.0 three-device repair, early local
reply, random audition, input guards and office speaker profile. Version
identifiers and generated About text are unified at 1.2.0; the version-freeze
step does not change musical or interaction behavior.

The fresh pinned build and release regressions pass. The exact 1.2.0 package
was installed and validated on three devices while retaining their bootloaders
and Recovery. The source is squash-merged into `master`; annotated tag
`v1.2.0` identifies that integrated commit rather than the former local release
commit. See
[release details](releases/v1.2.0.md) and [development](development.md) for the
artifact identity, original-mode switch and validation scope. Private device
snapshots, local paths and experiment logs remain outside tracked release files.

## Published version

The existing GitHub firmware download remains at v1.1.0. A GitHub Release and
firmware assets for 1.2.0 have not been published. README links continue to
point to the available v1.1.0 download until firmware publication is authorized.
The v1.1.0 and older tags/assets remain unchanged.
