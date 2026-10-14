# Optional device speaker profile

Run `sh tests/speaker_voicing/run.sh`; use `--sanitize` for ASan/UBSan/LSan.
Leak checks must run outside a ptrace sandbox rather than being disabled.

The tests exercise real preparation and PCM. They catch changes to reference
Source/Foreground, shared target and plan, leaked profile state, muted or
nonfinite FULL output, incomplete publication after invalid input/allocation
failure, unnecessary Dust allocation, loss of the intended note/harmonics and
folded high harmonics. Removing the Harmony upper-harmonic Nyquist guard is
confirmed to fail the explicit folded-frequency assertion.

The retained exact Harmony phrase also checks the introduced candidate-crest
regression: driven crest stays below 12 dB and output retains useful nonzero
energy within the .60 sample ceiling. The initial in-phase candidate was
15.57 dB; the chosen fixed-phase candidate is 11.23 dB. This is a digital
waveform constraint, not diagnosis or acceptance of acoustic distortion.
Removing the fixed Harmony phase offsets is confirmed to fail this crest test.

Real job ownership, mutual exclusion with the older bass-only filter,
all-or-nothing publication, common bars, failure retention, stale jobs and
Pause are covered by `sh tests/band/run_speaker_voicing.sh`. Real device-owner
queues and the preparation worker are covered by `tests/device_runtime`.
The default renderer and cues continue through the existing audio suites.

Numerical profile selection, native simulator checks and physical trial state
are recorded in `../../docs/verification/loudness-review/README.md`.
