# Local audio synthesis and scheduling

`components/echoy_audio` consumes immutable `echoy_bar_plan_t` copies. The DSP is
C11 and has no heap allocation, I/O, OS, UI, or core mutation after initialization.
Allocate `echoy_audio_size()` bytes once, with malloc-compatible alignment, then
call `echoy_audio_init(storage, size, output_epoch_us)`. The host currently uses
702696 bytes, including 64 logical voices, 192 pending events, three independent
15000-sample delays, the two-second deterministic noise buffer and wave tables.
One logical chord voice contains up to three pitches and their triangle partials.
Firmware allocates this once in PSRAM and reports allocation failure to its caller.

## Signal path

The six kinds follow the pinned `DuijuSynth.note()` independently:

- Kick: sine, exponential 128-to-47 Hz sweep, 4 ms exponential attack and stretched
  decay/source lifetime.
- Rim: 238/517/1034 Hz partials, levels .7/.23/.1, 1 ms attack.
- Hat: the original two-second Park–Miller noise, seed 1187, multiplier 16807,
  modulus 2147483647, starting at offset .2 s; expression-dependent highpass.
- Tone: fundamental/second/third sine partials at 1/.22/.07, 2400-to-700 Hz
  lowpass sweep, original duration plus release/source lifetime.
- Grain: the same noise starting at .65 s through a Q=5.5 bandpass, plus .19 sine.
- Chord: fundamental plus triangle warmth per pitch, gain divided by sqrt(pitch
  count), and distinct short/broken/answer envelopes and lowpass sweeps. The
  current core has no `flow` articulation; `ECHOY_ART_NONE` uses the original
  default chord envelope. No supported core event is mapped to a generic sine.

Sine interpolation uses a 4096-point table. Five triangle tables retain 1/3/7/15/31
odd-harmonic cutoffs, selected below Nyquist. Filter coefficients update every 16
samples during sweeps; the channel high shelf updates every 64 samples. Low/high
pass Q is interpreted in dB and bandpass Q linearly, following the
[Web Audio biquad definition](https://webaudio.github.io/web-audio-api/#filters-characteristics).
These bounded approximations are measured below, not described as sample-identical.

Each device has dry=.5, delay=.3125 s, feedback=.18, 2900 Hz lowpass damping,
wet=.2, volume and mute gain. The channel high shelf follows expression at 1000 Hz.
Master starts at .55. Center pan produces identical stereo channels; the physical
ES8311 receives its own local device, not a sum of remote speakers. Volume/mute
smooth over 18 ms, expression color/master over 25 ms. Muting/cancelling stops that
device's sources after at most 30 ms; other devices' sources and tails continue.
A bounded output clamp records clipped samples. It is not a port of the browser's
shared dynamics compressor, whose cross-device effect is not physically available
on independent speakers.

## Timing and ownership

The audio owner alone calls DSP methods. Control/UI/network owners send value
copies and never hold a UI lock in rendering. `echoy_audio_plan` uses `events` for
sound and leaves `motif` untouched. It schedules each device/bar once, validates
input, and rejects an older clock version from the same boot. A new clock version
cancels that device's pending events. A rest does not truncate an earlier tail. A device missing from a fully validated
plan has its pending notes cancelled and its own sources smoothly stopped/muted;
other devices retain their tails. Slots expiring on the current sample are
available immediately to new notes on that same sample.
Expression at onset scales duration by
`(1 + live_expression*.45)/(1 + planned_expression*.45)`; active envelopes remain
fixed, while the channel color/volume continue to move smoothly.

For session rate correction, target microseconds are
`epoch_us + (bar*2500000 + step*156250)/(1 + rate_ppm/1000000)`.
The target sample is `round((target_us-output_epoch_us)*48000/1000000)`, retaining
fractional microseconds in the plan path. Events trigger inside a block. A late
event is counted and discarded; a discontinuity uses `echoy_audio_skip_to` rather
than rendering/replaying missed time. `echoy_audio_presentation_us` reports the
cursor's mapped output time, not callback time.

`echoy_output_*` separates the backend and owns four preallocated rotating 256-frame
PCM buffers, checked open/write/close calls, and an explicit retry state. It writes
48 kHz, 16-bit, stereo blocks of 1024 bytes. A write failure stops further writes;
reopen/close failure preserves the error and retry requirement. Every failed open
is followed by checked close, including partially opened codecs. Cleanup failure
blocks another open until close succeeds, so a stale backend opened flag cannot
make a retry falsely appear initialized. It has no pointer
to core state and cannot change valid motif/data into a data fault.

`echoy_speaker_start(local_device)` creates the firmware audio worker. Its four-message
static queue accepts copied plans through `echoy_speaker_publish`; only the selected
local device is synthesized. `publish` and `retry` must have one control-task caller
and return timeout on a full queue instead of blocking. The worker processes at most
four messages per iteration. Kernel queue metadata stays in internal RAM. Its codec
backend calls `bsp_audio_codec_speaker_init`, `esp_codec_dev_open`,
`esp_codec_dev_set_out_vol`, `esp_codec_dev_write`, and `esp_codec_dev_close`, checks
all results, and never initializes a microphone codec. `echoy_speaker_status` returns
a copied status; control integration must dispatch `ECHOY_OUTPUT_FAULT`, never
`ECHOY_DATA_FAULT`, on output failure. Explicit retry calls `echoy_speaker_retry`.

Task 9 must start this worker, publish control plans often enough to precede their
sample deadlines, expose retry, and map status into the core/UI. The component is
in the IDF build; the current scaffold `main.c` does not yet start the worker.

## Actual buffering limitation

The pinned BSP privately calls `I2S_CHANNEL_DEFAULT_CONFIG`, which in the configured
IDF checkout is **6 descriptors x 240 frames = 1440 frames = 30 ms**. Its public API
accepts an I2S format but provides neither DMA sizing nor TX handles. No sibling BSP
files were changed. The four software buffers are rotating write storage, not a
claim that the hardware queue is four 256-frame blocks. The plan queue is four
messages; `esp_codec_dev_write` is synchronous and the BSP owns DMA buffering.

Status and startup logging record both depths. The first output cursor uses a
configured 30 ms DMA lead after codec open. This is a configuration-derived timing
estimate, not a measurement of actual DAC output or queue occupancy. Already
submitted PCM cannot be cancelled through the BSP API; clock invalidation cancels
not-yet-rendered events, with rendered DMA audio bounded by the hardware queue.
External capture must measure actual onset/underrun behavior, subtract queued audio,
and calibrate the mapping in Task 10. Achieving exactly 4x256 hardware DMA requires
an approved BSP API change, not an unreported alteration of the dependency.

## Reproducible artifacts and checks

```sh
cmake -S pc -B build-host
cmake --build build-host
ctest --test-dir build-host -R dsp --output-on-failure
ctest --test-dir build-host --output-on-failure
mkdir -p output/generated/audio
build-host/echoy_audio_render output/generated/audio
node tools/render_audio_reference.mjs output/generated/audio
python3 tools/compare_audio.py output/generated/audio --report tests/fixtures/audio/metrics.json
```

The Node tool uses local Chromium's real OfflineAudioContext, checks the pinned
HTML SHA-256, extracts the unchanged synth, and emits both its original compressor
path and an explicitly compressor-bypassed measurement path. It saves Chrome version
and source provenance. No fake JS DSP supplies the reference. Chrome requires local
process/socket permission. The comparison tool requires NumPy; normal CTest uses
only standard Python. WAV/f32 outputs are regenerable artifacts ignored by Git;
measured JSON metrics are versioned.

There are 12 two-bar phrase WAVs (9 s each), six isolated kinds, three extra chord
articulations and one block-aligned kick probe (4 s each). Each also has browser
WAVs for listening. Metrics include scheduled/threshold onsets, last audible sample,
RMS, peak, 20 ms RMS envelope error, waveform error, whole-render FFT spectral
centroid and energy in 0–200/200–2000/2000–6000/6000–24000 Hz bands. Threshold onset
uses 1/32768; C is quantized s16 while the browser metrics retain f32, so a one-LSB
onset is not interchangeable with the exact scheduling sample.

Measured with Chrome 151.0.7922.75 at 48 kHz: compressor-bypassed RMS difference is
at most .254 dB, 20 ms envelope NRMSE at most .0557, and maximum band-energy fraction
difference .0099. These figures do not prove perceptual equivalence. Unaligned kick
has waveform NRMSE 1.94465; moving its onset from sample 4800 to 6144 (a 128-sample
boundary) reduces this to .000535. The controlled probe supports a browser
block-relative frequency-automation phase effect; it does not justify shifting C
onsets or claiming waveform identity. The original compressor path remains in the
report, including its additional onset latency.

The 120 s three-device, maximum-activity, all-phrase density run starts all 896 notes,
peaks at 7/64 logical voices and .183259 PCM, with zero missed/capacity/clipped
samples. A separate adversarial 64 simultaneous long three-pitch chord test retains
all 64 voices without capacity loss; its output is bounded by the local clamp.
On this PC, the density run's unoptimized mean block was .119 ms and maximum .408 ms.
These are host observations, not ESP32-S31 timing guarantees. Real MCU CPU/DMA load,
PSRAM contention, speaker gain, human A/B listening and multi-device onset remain
Task 10 hardware acceptance work; no listening or physical audio capture was done.
