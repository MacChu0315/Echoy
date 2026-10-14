# Optional device speaker profile

Run `sh tests/speaker_voicing/run.sh`; use `--sanitize` for ASan/UBSan/LSan.
Leak checks must run outside a ptrace sandbox rather than being disabled.

The tests exercise real preparation and PCM. They catch changes to reference
Source/Foreground, shared target and plan, leaked profile state, muted or
nonfinite FULL output, incomplete publication after invalid input/allocation
failure, unnecessary Dust allocation, loss of the intended note/harmonics and
folded high harmonics. Removing the Harmony upper-harmonic Nyquist guard is
confirmed to fail the explicit folded-frequency assertion.

Following the user's rejection of the first office Harmony as too loud/bright,
the A4 spectral check limits the third-to-fifth harmonic energy relative to
the fundamental/second harmonic. The old profile fails that assertion. The
retained exact phrase must now have real pre-limiter headroom at .761111 volume
and lower, nonzero PCM energy. This replaces its superseded >.23 RMS target.

The quieter profile's relative crest is 12.58 dB, slightly above the preceding
profile's 11.23 dB, but its driven peak is only .57238 and requires no limiting
on this phrase. Requiring the old <12 dB crest ratio would reject a waveform
that has more actual headroom. The regression therefore checks driven peak
below .60 and useful RMS below .22. These are digital constraints, not diagnosis
or acceptance of acoustic distortion. Removing the upper-harmonic Nyquist
guard is confirmed to fail the second-harmonic folded-frequency assertion.

Real job ownership, mutual exclusion with the older bass-only filter,
all-or-nothing publication, common bars, failure retention, stale jobs and
Pause are covered by `sh tests/band/run_speaker_voicing.sh`. Real device-owner
queues and the preparation worker are covered by `tests/device_runtime`.
The default renderer and cues continue through the existing audio suites.

Numerical profile selection, native simulator checks and physical trial state
are recorded in `../../docs/verification/loudness-review/README.md`.
