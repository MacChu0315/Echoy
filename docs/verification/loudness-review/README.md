# All-role loudness investigation

The user reports residual Harmony distortion at approximately 80% and now
requests greater loudness for **all four roles and all three expressions**.
The intended use is an ordinary office, on a desk at 30–50 cm. This supersedes
the earlier request to retain overall volume. Merely reducing the final volume
does not meet the new goal.

## Current physical evidence

Before this investigation, left ran Harmony 500 Hz trial `abcdec27b6fe`; right
remains on ordinary `09275b935ad8`. The thinner Harmony timbre is accepted and distortion improved,
but repeats at roughly the same sound each loop. The subsequent exact UI target
is .7611111111111111 on the same left boot, revision and plan bytes, rather than
an assumed .8. See `../harmony-bass/residual-observation.json`.

The audit itself initiated no playback or microphone capture. The approved
left-only follow-up profile is installed below. The previous two microphone attempts remain
exhausted and their office-speech-contaminated metrics are not tuning evidence.

## Digital audit

`output/loudness-review-20261014/levels/run.py` compiled and exercised the real
C synthesis and device PCM, including the installed Harmony-only 500 Hz helper.
It covers six rhythm seeds, all 12 expressions, densities .5/1 and UI .65/1,
plus two exact-residual-plan cases. These are 578 **steady single-tile** cases,
not a model of the physical speaker, onset, crossfade, recording cues or linked
playback. Existing build inputs and exact-plan hashes are in
`source-manifest.json`; per-expression statistics are in `digital-audit.json`.

At full UI volume, compared on identical plans:

| Role | Offline gain probe | Median PCM RMS change | Interpretation |
| --- | --- | --- | --- |
| Pulse | 1.6 → 2.0 | +0.46 dB | Already strongly limited; little gain-only benefit. |
| Voice | .52 → .78 | +1.72 dB | More average output through greater compression. |
| Dust | .0317 → .0634 | +6.02 dB | Peak at most .32063; clear digital headroom. |
| Dust | .0317 → .0951 | +9.54 dB | Peak at most .48094; still below .60. |
| Harmony | Exact plan, UI .761111 → 1 | +0.504 dB | Most additional gain becomes limiting. |

RMS is not perceived loudness or a cross-role matching criterion. Dust is
spectrally different and was previously matched by listening; its headroom
does not justify arbitrarily maximizing its gain. All candidates need listening.

For the exact residual Harmony plan, 99.918% of PCM energy lies in 300–1000 Hz;
only .01139% is below 300 Hz. This does not prove which frequency/component
causes the audible breakup. Repeatedly cutting the accepted bass or increasing
overall drive has no established path to the requested louder, cleaner result.

Verification from the executed audit: 578 outputs remain within the digital
ceiling, no out-of-range samples, 14 single-sample versus 240-frame conversions
are byte-identical, and FFT energy agrees with time-domain energy. Two retained
loop periods differ by at most one int16 step. The initial assumed `target_rms`
metadata was corrected to the actual C field and rerun; all other 578-case
numeric fields are unchanged. See `validation.json` and `findings.json`.

## Hardware bounds and uncertainty

The pinned board/codec/I2S source and the official V1.0 schematic were checked.
No obvious S31 slot, shift or sample-byte-order error was found. Hardware volume
90 → 100 adds 5 dB **after** the app limiter, so .60 does not protect against
that increase in physical drive. Filling in schematic PA gain would instead
reduce the driver's DAC level at the same volume setting. Neither is a free
clean-loudness fix. Exact calculations, source versions, official links and
conditional-model caveats are retained in `hardware-review.json`.

Amplifier supply under load, actual fitted components, speaker impedance,
rating/response and acoustic output remain unmeasured. This investigation
cannot promise a numeric clean acoustic gain or diagnose a defective part.

## Approved direction and remaining checks

The user approved the broader device-voicing trial after reviewing this scope:
retain the notes/envelopes and expression rules, add moderate upper harmonics
to Pulse/Voice/Harmony, and raise Dust's trim conservatively. This changes
musical timbre beyond the already accepted Harmony-only bass cut. The first
physical trial remains left-only; ordinary office listening at 30–50 cm is the
target. The accepted direction is recorded in
`../../plans/2026-10-14-office-speaker-voicing.md`.

The first two bounded offline candidates have not demonstrated all-role higher
digital loudness with less limiting. In particular, the second candidate's
exact Harmony plan has a 5.12 dB greater pre-limiter crest, explaining why its
peak limiter removes so much average output. The bounded fixed-phase experiment
reduces that introduced crest while keeping one fixed setting per role. The
selected gains are `1.6/.78/.0634/1.6`; exact waves/phases are in the approved
plan and `phase-review.json`. This is not a finding about the physical cause of
the original breakup.

Full-volume paired mean R128 changes over 12 cases per expression are:

| Role | Expression 1 | Expression 2 | Expression 3 |
| --- | ---: | ---: | ---: |
| Pulse | +1.48 LU | +1.51 LU | +1.70 LU |
| Voice | +1.87 LU | +1.83 LU | +2.07 LU |
| Dust | about +6.02 LU | about +6.02 LU | about +6.02 LU |
| Harmony | +.18 LU | +.42 LU | +.08 LU |

Harmony expression 3 ranges -.1 to +.3 LU and is essentially at parity.
The exact residual plan at .761111 has .251812 PCM RMS, -.77 dB versus the
previous profile, but its R128 result increases .4 LU as energy moves upward.
About 62.87% of its PCM energy is now in 1–3 kHz. The timbre change is substantial;
limiting is still stronger than the original. These numbers do not establish
that every case is physically louder or that residual distortion is solved.

## Implementation and validation

`echoy_audio_voice_for_speaker()` prepares a scratch FULL replacement, retains
the original Source/Foreground/normalization target and copies only after all
validation succeeds. It skips unchanged Dust synthesis. Job guards prohibit
double filtering, calibration-after-handoff and partly published multi-bank
results. The worker selects the default-off `CONFIG_ECHOY_OFFICE_VOICING_TRIAL`
and its separate gain table; it cannot coexist with the older bass-only trial.
Temporary scratch is approximately 567 KB on the host, allocated off the PCM/UI
threads. Real device stack and preparation timing remain to be checked.

- `sh tests/speaker_voicing/run.sh` and `--sanitize`: isolation, invalid/alias
  inputs, allocation failure, legal pitches, independent harmonic probes,
  explicit Nyquist rejection and retained-phrase crest pass. A deliberate
  removed-Nyquist-guard mutant fails the folded-frequency assertion; removing
  the fixed Harmony phase offsets fails the retained-phrase crest assertion. LSan
  initially refused the ptrace environment; the complete run passes outside
  that sandbox with leak checks retained.
- `sh tests/band/run.sh`: original and six new speaker groups pass. Band and
  speaker suites also pass ASan/UBSan/LSan.
- `python3 -m unittest discover -s tests/device_runtime -v`: all 51 pass normally
  and with `ECHOY_RUNTIME_SANITIZE=1`. After final parameter selection, the eight
  office/config tests and 23 office lifecycle cases pass again with all three
  sanitizers and unchanged source hashes; see `runtime-final-validation.json`.
- `sh tests/audio/run.sh` and `sh tests/harmony_voicing/run.sh`: existing
  synthesis, ownership/error, performance/extreme-pitch and 500 Hz cases pass.
  Independent review also confirms 12 ordinary plans and three cues are
  byte-identical against the preceding committed implementation.
- Bounded phase comparison: 582 screening and 456 fixed-profile cases; all
  follow-up block partitions and repeated PCM periods agree. The actual product
  helper matches the selected prototype FULL and PCM byte-for-byte in 152
  cases. See `phase-review.json` and `production-comparison.json`.
- `python3 tools/check_desk.py --build-dir output/build-sim-office-voicing
  --output output/office-native-check`: 13 official native GSP/control/audio
  groups pass with zero late blocks and exact paused silence. A recorded copy
  of the real desk adds the actual profile/calibration and device PCM calls;
  it runs with `--silent`, so no computer playback occurred. This is one combined
  desktop PCM chain, not three separately measured physical speakers.
- Trial `system-update-bundle` run `20261014-150322-system-update-bundle-2`
  and final configured CMake builds of trial/ordinary firmware pass. The only
  warning is the retained unused factory-size check; the bundle targets OTA.
  UI data matches the preceding package byte-for-byte; frozen v4 SHA checks
  and `git diff --check` pass.

## Installed listening trial

After the user paused/separated both boards, official installation of
`7307f8ed9a3e` completed only on left `4553502d49524953010030eda0f452a0`.
ELF is `133f0e68982ff07bd35be1ac76f1dddefe5bc8df76e8bc3bc0e99f0f4f9a29c9`;
Boot ID `1063869759841904881`, operation
`0b67f292-8b2c-4135-95d9-f5cc309e3be8`. HEALTHY/validated, retained bootloader and
partition hashes, actual home, silent startup and magnetic baseline all pass.
There are zero startup crashes/partial writes/write errors/deadline misses.
See `package.json` and `physical-install.json`.

Right remains ordinary `09275b935ad8`. Both diagnostic capture entry points are
disabled. User listening, real prepared-profile workload and mixed-version
cooperation are pending; the user was asked first to compare all three Harmony
expressions at default and approximately 80% volume. Do not promote this trial
or describe all-role louder/cleaner output as accepted yet.
