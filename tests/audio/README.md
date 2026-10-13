# Portable audio checks

Run the local behavior, deterministic output, invalid-input and allocation-failure checks:

```sh
sh tests/audio/run.sh
ASAN_OPTIONS=detect_leaks=0 AUDIO_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' sh tests/audio/run.sh
```

The `detect_leaks=0` setting is needed only in the ptrace sandbox where
LeakSanitizer cannot run. The failure tests separately verify buffer ownership
and cleanup at every failed allocation.

Generate independent audio references by executing the unchanged frozen HTML
in the installed Chrome, then compare the actual C output:

```sh
node tests/audio/oracle.mjs /tmp/echoy-audio-oracle
python3 tests/audio/compare.py /tmp/echoy-audio-oracle
```

The oracle uses the existing Playwright module and Chrome installation. Override
`ECHOY_PLAYWRIGHT_MODULE` and `ECHOY_CHROME` if their local paths differ. It does
not install packages or change the prototype. The comparison needs the existing
NumPy package. Chrome needs permission to create local browser sockets, which
the restricted shell sandbox does not provide.

The corpus covers every role and phrase, the original Pulse 1 normalization,
the generic linked-plan path, all six note kinds, the three chord articulations,
late notes, delay tails, overload compression, tapping and recording cues.
The report retains every RMS, peak, waveform, spectrum and onset residual.
Float recordings and generated C fixture data stay in the requested temporary
directory. Buffer consumption and per-device scheduling belong to the mixer
and are tested at that boundary; this API prepares complete immutable buffers.
The tap reference is a fresh 24 kHz synth at sample zero. It does not claim
sample-identical output to an already-running native-rate live AudioContext,
whose compressor history and first render-quantum phase depend on tap timing.

The C port preserves the measured Chrome 151 audio behavior of the frozen
prototype, including first-quantum frequency automation for delayed kicks and
the feedback cycle's extra 128-sample quantum. Oscillator lookup interpolation
and floating-point math need not be bit-identical on another CPU/browser.
The compressor portion retains the upstream Chromium BSD license in
`echoy_audio.c`; include that notice with binary distributions.

`run.sh` also runs `run_performance.sh`. It uses real preparation jobs for all
four roles and counts expensive libm calls, rather than requiring a particular
host's wall-clock speed. The limits reject per-sample software-double math,
exponential envelope evaluation and sine/cosine evaluation. A MIDI 127 boundary
case also covers phase conversion for partials above the sample rate. For this
boundary check, use `-fsanitize=address,undefined,float-cast-overflow` in
`AUDIO_TEST_CFLAGS` when running the sanitizers.

The performance probe prints host CPU milliseconds for diagnosis; these are
not physical ESP32-S31 timing claims. To compare a separately retained old
source without applying the optimized operation-count assertions:

```sh
ECHOY_AUDIO_SOURCE=/absolute/path/to/old/echoy_audio.c \
AUDIO_TEST_CFLAGS=-DECHOY_PERFORMANCE_BASELINE sh tests/audio/run_performance.sh
sh tests/audio/run_performance.sh
```

Sound acceptance still requires `compare.py` against the unchanged frozen
HTML captures. Its RMS, peak, waveform, spectrum and onset budgets are unchanged.
The synthesis keeps double precision for event-to-frame rounding and final RMS
normalization; sample processing uses the chip's hardware single precision.

The public plan also accepts pitches outside the generated arrangements.
`run.sh` checks retained pre-optimization PCM samples for MIDI 0, 13, 117 and
127 Grain notes and MIDI 126 chords. Grain below MIDI 20 or at/above MIDI 114
keeps the original double-precision band-pass coefficients and state because
float rounding there changes its narrow resonance. Generated Dust pitches
stay on the fast path. Chords use unsigned phase accumulation to avoid drift
on long high notes.

For complete-loop regression across all 128 MIDI pitches of Tone, Grain and
Chord, provide the retained pre-optimization source:

```sh
python3 tests/audio/compare_baseline.py /absolute/path/to/old/echoy_audio.c \
    /tmp/echoy-baseline-comparison.json
```

This compares all 384 two-and-a-half-second cases using the same RMS, peak,
waveform, spectrum and onset budgets as the HTML oracle. It requires NumPy
and builds temporary host shared libraries. The compact
`filter_extremes_reference.h` fixture is sampled from the normalized full-loop
output of the original `5de1d26` synthesis, at a fixed 457-frame stride;
`baseline_probe.c` records its plan construction. Retaining the full-range
comparison does not replace the independent frozen HTML sound check.

For a distortion investigation, observe the actual C synthesis and device PCM
stages without playing sound or writing a device:

```sh
python3 tests/audio/probe_stages.py /tmp/echoy-stages --seed 40aa --phrase 2 --volume .34
python3 tests/audio/probe_stages.py /tmp/echoy-exact-stages --plan-bin /path/to/plan.echp --volume .34
python3 tests/audio/probe_stages.py /tmp/echoy-json-stages --plan-json /path/to/music.json --volume .34
```

Use an empty output directory. `--plan-bin` accepts the complete canonical ECHP
payload used by `echoy_link_plan_encode`; `--plan-json` accepts native numeric
music JSON (as printed by `tests/music/runner.c`), optionally wrapped in
`{"music": ...}`. Both retain full linked events and pitches. Standalone seed,
role, phrase and density cannot recreate an arbitrary linked plan. Set
`--original-pulse1` only for an unlinked local Pulse phrase 1.

The probe inserts three observation calls into a temporary copy of the current
synthesis C. It also builds the unmodified production source and requires the
normalized bank, HP/drive, limiter gain and integer PCM to be byte-identical.
PCM runs through `echoy_device_pcm_convert`, with 1-sample/240-sample partition
equivalence checked. No production source or parameter is edited. The input
is the steady single-tile FULL bank path, with the production float bank trim,
double UI volume and .45 bus multiplication. One loop warms the PCM state; two
loops provide the reported steady measurements. This does not reproduce
crossfades, volume smoothing, cues, multiple audible tiles or device deadlines.

Outputs include the exact `plan.echp`, raw `.f32`/`.s16` samples, `report.json`
with source hashes, peak/RMS, frequency energy, loop-boundary jumps, effective
limiter gain reduction and gain-aligned shape residuals. Effective reduction
uses delayed input above -80 dBFS and .1/1/3/6 dB thresholds; it does not treat
denormal tails or a slightly sub-unity float envelope as audible limiting.
Shape residual measures departure from constant gain, including intended
compression; it is not THD and cannot establish acoustic distortion.

The `*_matched.wav` files use one common RMS, normally -24 dBFS, reduced for all
files together if needed to keep every peak <= .8. Their individual gains are
recorded. Compare these for timbre/dynamics rather than the much different raw
levels. The probe never plays them automatically. Hardware listening and the
exact reported plan remain necessary for a sound-quality conclusion.
