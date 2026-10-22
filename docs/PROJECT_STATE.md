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
one device with the reviewed Vibe Mode 0.2.0 base. Six startup tests, 54
device-runtime tests and the board-audio checks pass. The original faulty
archive remains preserved. The fixed candidate and validation boundaries are
listed in [migration details](iris-0.2-migration.md). Final physical interaction,
listening and multi-device acceptance remain separate from startup acceptance.

The user subsequently reported continuous vibration. Echoy had no motor
initialization despite having no haptic feature. It now explicitly disables the
motor before other startup work; failure keeps Iris available and stops the
application. The replacement candidate passed healthy installation, and the
user confirmed that continuous vibration stopped after returning to Echoy.

Vibe Mode independently reports LittleFS `Corrupted dir pair at {0x0, 0x1}`
and mount error `-84`. A bounded metadata probe found nonempty NAND data without
a LittleFS marker in the first two logical pages; this does not establish that
the rest of the medium is empty or recoverable. No format or erase was performed.
Storage recovery remains pending a data-preservation decision and investigation.
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
