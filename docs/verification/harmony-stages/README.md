# Harmony stage observations (2026-10-13)

The retained device profile changes the waveform substantially in its final
limiter on the representative `40aa`/.5 plans, even at UI volume .34. This is
an identified digital change to compare by listening, **not proof that it causes
the user's current distortion**. The initial representative investigation did
not have the actual linked plan; the later device capture below now preserves
it. No production audio parameters, synthesis, frozen HTML or dependencies
were changed by the probes.

## Exact linked device sample

The user completed the requested right-side Harmony/Arpeggio reproduction on
package `73058c98b7ce`, boot `7746524967332329690`. Read-only ECA1 captured a
committed, following, continuously playing plan with 10 events and six source
notes at exact target volume .65. Complete linked pitches/durations are retained
in [device-plan.echp](device-plan.echp); identity, token and current state are in
[device-snapshot.json](device-snapshot.json). This is the current reproduction,
not an assertion that its pitches match the earlier .34-volume complaint.

`probe_stages.py --plan-bin` verifies observer/production and block-partition
byte identity. Pre-limiter peak is 1.91249; final peak .599994 and RMS .308077.
Maximum effective attenuation is 10.069 dB; 67.4% of audible input samples have
more than 1 dB attenuation. Shape residual against a constant gain is 29.7%,
which describes dynamics changes and is not THD. The shared compressor is
effectively constant gain (relative residual 3.47e-8), and the loop boundary
jump is one int16 count. [Full report](device-report.json).

The user compared average-level-matched five-second pre-limiter and final PCM
WAVs and explicitly reported no obvious distortion in either. This did not
reproduce the complaint outside the device. It supports investigating the
level-dependent device output path next, but neither identifies a failed
component nor proves all digital processing is perceptually transparent.
The user then lowered only the right volume and selected the highest position
they considered clean. Read-only capture confirms the exact same ECHP SHA256
and target volume `0.12222222222222229`, still following and looping. This is
a user-selected point for one phrase, not a universal maximum for the speaker.
The exact replay at this setting does not engage the limiter. See
[clean snapshot](device-clean-snapshot.json) and [clean replay](device-clean-report.json).
No production output parameter was changed; there is not yet an acoustic fix.

## Actual path and scope

`echoy_audio.c` renders the chord oscillators, note filters and envelopes, mixes
the dry/delay signal, applies the shared Web Audio compressor, fades the last
60 ms of Harmony, then normalizes each bank to a target RMS of at most .045.
`echoy_band_calibrate_job` scales only the immutable FULL bank by Harmony 2.38.
That calibration is after the shared compressor and normalization; it cannot
be corrected by their peak or RMS bounds. `echoy_band_render` multiplies the
bank by smoothed UI volume and the .45 bus. The device stage applies its 120 Hz
high-pass, drive18, and .60 limiter with 1 ms lookahead / 40 ms release, then
converts to int16. Hardware volume 90 follows the PCM stage.

The new `tests/audio/probe_stages.py` compiles these production synthesis and
PCM sources. Three read-only observation calls are inserted into a temporary
copy of synthesis. Each run also builds unmodified synthesis and requires all
normalized bank, HP/drive, gain-envelope and PCM bytes to match. The observer
reads the real PCM state after each sample and verifies that the reconstructed
gain produces the exact int16 result. Processing in 240-frame blocks must also
match. Production code remains unchanged.

This is the steady single-tile FULL-bank path, not a replay of scheduler
transitions, cues, volume smoothing, multiple simultaneous tiles or hardware
timing. It uses the exact float calibration then double volume/bus ordering.
One loop warms PCM state; two loops (five seconds) are measured. Complete ECHP
or native numeric music JSON input supports already-linked plans without
reconstructing them from a seed. Every run saves the canonical plan and source
hashes. A separately generated real `echoy_arrange_linked` plan also passes.

## Representative results

All plans below are standalone seed `40aa`, density .5. They are not the failed
linked recording. All normalized FULL banks have RMS .045 and all output peaks
stay <= .6000, with zero post-limiter clipping counts.

| Expression | UI volume | HP/drive peak | PCM RMS | Maximum effective reduction | Shape residual |
|---|---:|---:|---:|---:|---:|
| Short chords | .10 | .509 | .0826 | 0 dB | .01% |
| Short chords | .34 | 1.729 | .1798 | 9.19 dB | 25.8% |
| Short chords | .65 | 3.305 | .2152 | 14.82 dB | 37.6% |
| Short chords | 1.00 | 5.085 | .2352 | 18.56 dB | 43.0% |
| Arpeggio | .10 | .300 | .0862 | 0 dB | .01% |
| Arpeggio | .34 | 1.021 | .2513 | 4.62 dB | 14.0% |
| Arpeggio | .65 | 1.952 | .3070 | 10.25 dB | 29.7% |
| Arpeggio | 1.00 | 3.003 | .3323 | 13.99 dB | 36.3% |
| Answer | .10 | .390 | .0858 | 0 dB | .01% |
| Answer | .34 | 1.327 | .2062 | 6.90 dB | 20.9% |
| Answer | .65 | 2.537 | .2352 | 12.52 dB | 32.3% |
| Answer | 1.00 | 3.904 | .2477 | 16.27 dB | 36.5% |

Effective gain statistics exclude delayed limiter input below -80 dBFS. Shape
residual is the RMS departure from the best-fit constant gain, divided by PCM
RMS, with the actual 24-sample lookahead aligned. It includes intended dynamics
changes and is **not THD**. The old cumulative `limited` counter can report
almost every sample even when meaningful gain reduction occurs much less often.
At .34, active-sample fractions reduced by more than 1 dB are 49.0%, 25.0% and
44.8% for the three expressions. [Full measurements](checks.json).

For these plans, the shared compressor acts almost exactly as fixed gain
1.50615: aligned relative residuals are 1.0–2.0e-7. The pre-compressor peaks
are only .061–.096. The following normalization is also constant gain. These
specific inputs therefore give no evidence of overload inside shared synthesis.
This does not establish that every possible linked or densely overlapping plan
is equally linear.

No large jump appears at the repeating-bank boundary: at .34 the measured
integer PCM boundary jump is zero for Short/Answer and one int16 count for
Arpeggio. At full volume Arpeggio is two counts. The bank's end fade and initial
compressor delay are present. This argues against a loop-boundary click in
these cases; it does not clear live transitions or timing faults.

The high-pass leaves almost all energy above 120 Hz. At .34, pre-limiter Short
is approximately 53% in 120–300 Hz and 47% in 300–1000 Hz; Arpeggio/Answer are
approximately 95%/92% in 300–1000 Hz. This supplies no basis for another blind
sub-120 Hz adjustment. The representative expression ranking does not reproduce
the user's observation that Answer is somewhat better, reinforcing the need
for the exact linked events and a listening comparison.

## Next discriminating comparison and bounded candidates

The exact-device capture above completes the first prerequisite: preserve the
complete current reproduction plan, exact volume, and whether it is the
committed/current linked bank. Role/phrase/seed/density alone do not retain the
linked pitches and event durations. The stored ECHP has been replayed through
the probe; new reports must retain their own complete plan and volume too.

The completed comparison used `hp_drive_matched.wav` and `pcm_matched.wav`: both have
the same documented RMS, normally -24 dBFS, with all peaks <= .8. This holds
digital listening level constant while exposing the limiter's effect. Compare
`pre_compressor_matched.wav` and `normalized_full_matched.wav` if the character
is already audible before PCM. The probe does not play audio automatically.

If the limiter character reproduces the complaint, the smallest bounded
candidate is lowering Harmony's pre-limiter drive (its bank trim), retaining
pitch/rhythm, high-pass and .60 safety ceiling. For the representative .34
cases, a trim near .8 instead of 2.38 makes their largest peak about .581 and
avoids limiting, but loses approximately 9.5 dB of pre-limiter gain and will
still limit at higher UI volume. Avoiding all limiting at full UI volume for
even these three cases requires a trim near .28, with a large loudness cost.
Those numbers are diagnostic bounds for this corpus, not a chosen calibration
or a guarantee for other plans. A volume-dependent strategy would change the
control's response and needs its own explicit evaluation.

Changing release time or adding more gain does not remove the demonstrated
peak-to-ceiling gap. They should not be mixed into the first comparison. A
separate trial could evaluate release/envelope changes after the exact-plan
comparison establishes that their dynamics are the objectionable part.

If the same stored PCM is clean at low output level but distorted at a higher
output level, that isolates a level-dependent output-path contribution without
changing digital synthesis. It still does not identify a codec, amplifier,
speaker or mechanical component. No acoustic cure is claimed here.

## Reproduction and checks

```sh
python3 tests/audio/probe_stages.py /tmp/echoy-stages-p2 --seed 40aa --phrase 2 --density .5 --volume .34
python3 tests/audio/probe_stages.py /tmp/echoy-stages-exact --plan-bin /path/to/current.echp --volume .34
sh tests/audio/run_device_pcm.sh
```

The 12 standalone runs pass observed/unobserved byte equality and 1/240-frame
PCM partition equality. Native numeric JSON and canonical ECHP round trips
reproduce the same plan, bank, drive and PCM bytes. A linked Voice-to-Harmony
case also passes, as does its ASan/UBSan/float-cast-overflow run; leak detection
is disabled only for the documented ptrace sandbox limitation. The existing
device PCM suite passes including all 144 loops and 2,304 tap mixtures.
`python3 -m py_compile tests/audio/probe_stages.py` and `git diff --check` pass.
`sha256sum -c SHA256SUMS` in `reference/v4` passes all four retained files.

Local raw samples, matched WAVs, compile sources and detailed reports are in
`output/harmony-stages-20261013/final/`. The compact 12-case measurements are
retained here; generated binaries/audio remain outside version control.
