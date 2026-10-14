# Periodic Harmony 500 Hz voicing

`echoy_audio_cut_harmony_bass()` is an optional pure-C preparation helper for
one complete 60000-frame, 24 kHz loop. The device integration selects Harmony
FULL before role trim. Existing synthesis, normalization, gain and limiter
parameters are untouched. The helper does not inspect a microphone, allocate
memory or run per-sample transcendental functions.

The coefficients reproduce the approved offline second-order Butterworth
candidate, without gain compensation. The input remains immutable during one
full warm-up and a second full validation pass. If the four float boundary
states match bit for bit, the validated periodic pass is written in place.

Strict float-state equality alone is insufficient for every legitimate input:
a 5000 Hz sine with peak .2 and phase .73 has up to 1.78813934e-7 difference
between the final two periods of the independent 22-pass reference. Extra
warm-up does not remove this rounding cycle. For unequal states, the helper
runs the same original input through both adjacent-period states in parallel
and checks **every output sample**, not just the state at the boundary. It
accepts only differences at or below:

```text
min(4.7e-7, max(32 * FLT_TRUE_MIN, 32 * FLT_EPSILON * input_peak))
```

The absolute limit is below half a signed-16-bit sample after the conservative
gain-only budget `4 * .45 * 18`:
`4.7e-7 * 4 * .45 * 18 = 1.5228e-5 < .5 / 32767`. This is a numerical budget,
not a physical audio or full downstream-filter guarantee. The relative limit
shrinks with the actual bank peak; the tiny floor only handles float underflow.
The checked later pass is then replayed in place. The maximum work is three
read-only source passes (the third advances two states) plus one write pass.

Null/wrong-sized input, nonfinite input/output, arithmetic overflow, and period
error above the budget return false before modifying any sample. A finite .8
peak version of the rounded 5000 Hz fixture exercises budget rejection and
verifies byte-for-byte preservation. Ordinary prepared banks use the exact
path; the selected captured Harmony candidate still matches its archived
22-pass output byte for byte.

Run:

```sh
sh tests/harmony_voicing/run.sh
sh tests/harmony_voicing/run.sh --sanitize
```

Tests independently derive coefficients and the analytic frequency/phase
response, compare against 22 immutable reference periods, rotate the source
across the wrap, exercise zero/DC/boundary impulses, forbid heap operations
during the helper, and prepare all twelve existing role expressions. Source,
foreground, target RMS, plan notes and repeated unfiltered preparation remain
unchanged. Sanitizers include ASan, UBSan and LSan; leak checking must run
outside the environment's ptrace restriction.

To compare the selected local candidate as well, append these two filenames
to either command:

```text
output/harmony-listening-20261014/eq-candidates/hp500-v0.650000000/normalized_full.f32
output/harmony-listening-20261014/eq-candidates/hp500-v0.650000000/filtered_full.f32
```
