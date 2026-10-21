# Echoy project state

## Iris 0.2 migration candidate

This branch prepares version 1.2.1 against a pinned matching Iris/Vibe Mode 0.2
workspace, preserving the accepted v1.2.0 application behavior. The candidate
build, offline package checks, C/PCM regressions and native three-owner checks
pass. The 1.2.1 candidate was successfully saved as a Mosaico platform draft;
it has not been submitted for review. No device transition has been performed.
See [migration details](iris-0.2-migration.md) for the changed contracts and the
approval required before the first device transition.

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
