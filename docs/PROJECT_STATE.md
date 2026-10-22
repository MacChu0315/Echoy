# Echoy project state

## Iris 0.2 migration candidate

This branch prepares version 1.2.1 against a pinned matching Iris/Vibe Mode 0.2
workspace, preserving the accepted v1.2.0 application behavior. The candidate
build, offline package checks, C/PCM regressions and native three-owner checks
pass. The 1.2.1 candidate was saved as a Mosaico platform draft; the user has
since reported that platform review rejected it for startup/memory, rendering
and LittleFS failures.

An authorized single-device test on 2026-10-22 installed the reviewed Vibe Mode
0.2.0 base and the exact uploaded candidate. The application image was written,
but its 45-second healthy-acceptance check failed. Live identity confirmed
Echoy 1.2.1 and the expected ELF hash. All 36 rendering samples from approximately
8 to 183 seconds after boot reported 0.0 fps; the screen was incomplete, and the post-startup snapshot
RPC was unavailable. No valid retained core dump or `ESP_ERR_NO_MEM` was observed
in the captured evidence; the precise application stall remains unlocalized.

The device successfully returned to Vibe Mode 0.2.0. That boot separately
reported LittleFS `Corrupted dir pair at {0x0, 0x1}` and mount error `-84`.
Its cause and relation to the review environment remain unverified. No storage
format or application fix was performed. The candidate is not physically
accepted; recording, audio, touch and multi-device acceptance remain pending.
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
