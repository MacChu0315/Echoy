# Office speaker voicing proposal

Status: the user approved the broader timbre direction on 2026-10-14, explicitly
prioritizing louder/less-distorted output and a left-only first trial. Product
implementation and local regressions are complete; the selected left-only trial
`7307f8ed9a3e` was installed with healthy, calibrated, silent startup. The user
accepts Pulse/Voice/Dust but found Harmony too loud and bright without obvious
breakup. Its Harmony-only refinement `5d4776c9ae6d` is now installed and
calibrated. The user explicitly accepts that refinement's sound/balance and
reports no obvious breakup, completing the current sound-tuning round.
Retain the accepted device configuration; the user-requested right-device
rollout is complete on the same package, with startup and calibration verified.
The confirmed implementation baseline is commit `0953c52`; a fresh same-profile
physical cooperation check remains pending. Existing firmware-update authorization remains
valid; do not request the same sound-direction approval again.

## Goal and acceptance

The user wants all four roles and all three expressions to be clearer and louder
on a desk at 30–50 cm in an ordinary office, while resolving the remaining
repeatable Harmony breakup. Reducing the final output alone is insufficient.
No calibrated acoustic-gain target is claimed without physical measurement.
The final judgment is the user's comparison at that distance and setting:
clearer/louder musical phrases, no obvious recurring breakup and useful volume
control. A numerically higher PCM RMS alone is not acceptance.

## Proposed sound changes

- Pulse: reduce the low kick fundamental and introduce its third/fifth
  harmonics. The drum becomes lighter/brighter; rim and hat synthesis remain.
- Voice: strengthen the existing second/third harmonics and raise its level
  moderately. The sound becomes brighter while preserving each note/envelope.
- Dust: retain its synthesis and first try twice the current trim. This is
  +6 dB digital level, not a promise of twice the perceived loudness. Its
  final balance must be checked by listening because prior balance was acoustic.
- Harmony: retain the accepted 500 Hz bass cut, add per-note second/third
  harmonics and reduce its pre-limiter trim in the first candidate. This is
  intended to offer a clearer alternative within the existing peak budget,
  not to claim the physical cause of the residual breakup is known.

The first offline candidate used these amplitude weights: Pulse kick
fundamental/third/fifth `.35/.40/.20`; Voice fundamental/second/third
`.65/.40/.20`; Harmony `.55/.35/.20` plus its original triangle component.
Its role trims were `1.6/.65/.0634/1.6`. It did not establish all-role higher
digital loudness with reduced limiting. A second bounded comparison strengthened
Harmony's third/fourth/fifth harmonics and reduced Pulse kick relative to rim/hat;
its tested gain range also failed that joint numeric criterion. The exact
Harmony plan exposed a 5.12 dB increase in pre-limiter crest, motivating one
bounded fixed-phase comparison instead of another gain increase. All remain
experimental, not frozen production constants or established acoustic fixes.
Candidate metrics must be reviewed before selecting a physical listening
profile; lower digital RMS alone does not disprove clearer speaker reproduction.

The selected listening profile retains the second candidate's weights:
Pulse kick H1/H3/H5 `.15/.18/.10` and rim/hat envelope levels ×1.3;
Voice H1/H2/H3 `.65/.40/.20`; Harmony H1–H5 `.35/.25/.35/.20/.10`
plus its original triangle component. Dust synthesis is unchanged.
Fixed phase offsets are Pulse H3/H5 `(pi/2,3*pi/2)`, Voice H2/H3
`(3*pi/2,0)`, Harmony H2–H5 `(0,3*pi/2,0,pi)`; fundamentals retain original
phase. Role trims are `1.6/.78/.0634/1.6`.

The bounded comparison improves each role's mean weighted digital level versus
the original, but Harmony is marginal: its third expression ranges -.1 to
+.3 LU. Limiting is still stronger than the original. This is an approved
physical timbre trial, not proof that the louder/cleaner goal is achieved.

After that first listening, keep the accepted Pulse/Voice/Dust exactly as above.
The Harmony-only refinement uses `.60/.20/.10` for fundamental/second/third,
removes added fourth/fifth, retains second/third phases `(0,3*pi/2)` and the
triangle component, and reduces its trim to `.8`. The 500 Hz filter and common
PCM remain. This directly addresses the user's rejected sharpness and excessive
Harmony loudness; it is not a request to lower all roles. Numerical comparison,
physical installation and remaining listening checks are in
`../verification/harmony-softening/README.md`.

Notes, timestamps, note lengths/envelopes and expression rules remain. Add
harmonics separately to each synthesized note, never by distorting the mixed
chord. Exclude harmonics at/above Nyquist. Keep the original Source/Foreground
banks and shared target unchanged; changing FULL's waveform must not feed back
through shared normalization into the other banks. Keep the original renderer
and frozen HTML available unchanged when the device profile is disabled.

## Approved implementation boundary

Use an opt-in device profile, with computation confined to the existing immutable
preparation worker. This proposal adds no real-time task, UI control, waveform
format, persistence or wireless message. Preserve the common 120 Hz filter,
drive 18, peak ceiling .60 and hardware volume 90 for the first comparison, so
physical peak pressure is not raised merely to obtain a louder reading.

The offline prototype renders a replacement FULL bank after original preparation
to isolate it from shared normalization. That extra pass is a prototype cost,
not approval to slow production preparation or collaboration switching. Measure
the added work and use an equivalent bounded implementation before a device
trial; retain old sound while preparing and reject failed/stale jobs as today.
No microphone capture is included; the earlier two-attempt budget is exhausted.

## Validation and physical comparison

Compare all 12 expressions across multiple rhythms/densities and the preserved
exact failing phrase through actual C synthesis/PCM. Inspect peak bounds,
frequency-band energy, effective limiting, DC, boundaries and repeatability;
retain the original Source/Foreground bytes and plan encoding. Check recording
cues/tap auditions, stale/cancelled preparation, independent Pause and common-bar
switching. Check workload/memory against the accepted responsive firmware.
Run the matching native simulator and pinned SDK build before device updates.

First physical trial remains on the left, keeping the right reference image.
User pauses/separates both on a nonmetal desk before restart, then listens to
the exact retained phrase and representative expressions. Use the existing
official workflow and verify identity/Recovery/artifact/calibration. Do not
promote to both boards solely from digital metrics. If the speaker cannot
provide the desired cleaner/louder sound with an acceptable timbre, report
that limit rather than declaring a quieter setting a completed solution.
