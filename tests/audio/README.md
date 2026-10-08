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
