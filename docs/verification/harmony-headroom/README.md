# Harmony distortion and output headroom (2026-10-10)

The paired-match package `ea1b10c28cd4` had acceptable relative role volume,
but the user reported clearly distorted Harmony. This follow-up retains hardware
volume 90, PCM gain 6, role gains 1/1/.0672/2.38 and the frozen synthesis/UI.
Only the device limiter ceiling changes, from .90 to .60.

## Evidence before the change

The resumed session recovered the previously saved home capture and obtained a
fresh live capture of the same board and boot `16482991358666837799`. Harmony
phrase 3 (Answer), seed `40aa`, density .5 was at maximum volume, not .65.
The real C synthesis/scheduler/PCM replay peaks at 1.3472 before limiting there.

The user then reported clear distortion at knob y178 (about .640 volume).
Playback was observed, but the limiter counter stayed at 767,999 and all audio
write/partial/deadline counters stayed zero. The same production scheduler and
PCM converter replay has peak .8617, RMS .1850 and zero limited/clipped samples.
This rejects digital limiting as a necessary cause of the reported distortion.

The requested low-volume comparison actually landed at knob y211, about .342,
not the suggested .20. The user reported markedly reduced or absent distortion.
They subsequently selected y195, about .486, as the highest clean listening
position for this phrase. Its inferred pre-limiter peak is about .655. These
are quantized screenshot estimates and user listening observations, not measured
speaker voltage, SPL, THD or a universal clean operating limit.
[Listening evidence](listening.json), [distorted setting](distorted-device.png),
[selected clean setting](clean-limit-device.png).

Executing the frozen v4 HTML in Chrome and comparing this exact seed's Harmony
phrases 2/3 plus cues passes all 9 existing comparison budgets, without widening
them. The full Harmony waveform relative error is about .00001.
[Independent HTML comparison](html-comparison.json).
The user could not compare the exported WAV on computer headphones/speakers.
The observations are consistent with level-dependent board output distortion;
they do not isolate the codec, analog amplifier, speaker or board-specific path.

## Bounded output trial

Ceiling .60 leaves about 8% sample-amplitude margin below the selected .655 peak.
The existing 1 ms lookahead, 80 ms release, cancellation/reset and role calibration
remain unchanged. Strong Pulse/Harmony transients receive more gain reduction;
this can change perceived role balance and requires another listening verdict.
It is not a guarantee that all frequencies or sustained output are clean.

- `sh tests/audio/run_device_pcm.sh`: passes PCM continuity, release/reset,
  invalid input and ceiling checks; 144 synthesized loops and 2,304 tap mixtures
  stay below .60002 with no clipping. The prior gain-over-3x checks still pass.
  A new calibrated `40aa` regression covers .35/.49/.65/1 UI volume. It failed
  the .60002 peak bound on the old implementation, then passed with .60.
- `python3 tests/device_runtime/test_runtime.py`: 22/22 pass.
- `ECHOY_RUNTIME_SANITIZE=1 python3 tests/device_runtime/test_runtime.py`:
  22/22 pass with ASan/UBSan/LSan outside the ptrace sandbox. The first sandboxed
  run failed all cases because LeakSanitizer cannot operate under ptrace; no
  sanitizer was disabled for the successful rerun.
- `sh tests/band/run.sh`: scheduler and role-calibration suites pass.
- 240 production-profile cases include both matching seed `1128` and reported
  seed `40aa`, all roles/phrases, densities .5/1 and volumes .65/1. No clipping;
  sample peak <= .599993897, FFmpeg true peak <= -4.2 dBFS. All 96 prior raw
  synthesis banks are unchanged. [Checks](checks.json), [levels](levels.csv).
- At .65 UI volume, median RMS changes versus .90 ceiling are Pulse -1.53 dB,
  Voice 0 dB, Dust 0 dB and Harmony -1.02 dB. At full volume they are respectively
  -2.64, -.35, 0 and -2.04 dB. These are digital comparisons, not SPL readings.
- Build doctor verified ESP-IDF commit `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`,
  target ESP32-S31 and the manifest constraint. The existing official
  `system-update-bundle` build passes with no compiler warnings, retaining 15%
  application partition headroom. Full build log is retained locally at
  `output/harmony-distortion/build.log`.

## Installation

Package `44ad4e4a59ec` was installed on the same verified Mosaico using the
official workflow after Recovery initialization. Operation
`d0fd64fb-f4e4-4858-859c-963e28c3393c` succeeded, HEALTHY boot
`11202007909105101612`, with matching application ELF identity and commit record.
Bootloader/partition hashes and UI resources are unchanged.
[Artifact](artifacts.json), [device result](device-result.json),
[actual home](installed-home.png).

Automatic approval initially rejected the install because it did not recognize
the prior session's continuing authorization. The exact original user message
and matching project/device scope were verified before submitting the same
operation again; approval then passed. The rejected attempt performed no write.

Startup reaches the expected home with the original role gains and no audio
write/deadline errors. Existing GPIO/I2S initialization messages and the recovered
BQ27220 profile-read timeout are not claimed fixed by this audio-only change.
Post-installation playback used seed `8234`, including Harmony Answer and the
other three roles. Actual peak PCM is 19,660 with zero clipping, partial writes,
write errors or estimated deadline misses; render/conversion maximum 3,918 us.
The user reports distortion **markedly improved**, but all four roles are too
quiet. This trial therefore improves distortion but does not pass overall
loudness acceptance. Next, evaluate whether higher average output under the
same peak ceiling can help without upsetting role balance or sound quality.
