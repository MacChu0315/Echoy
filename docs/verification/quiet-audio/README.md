# Quiet audio investigation: evidence before another gain change

The investigation started from `d87ae02e9222` (hardware volume 90). The user reported
very quiet sound with the UI slider at maximum, and also with Voice/Dust roles.
The user's board had no known prior acoustic baseline. Initial investigation was
read-only; the subsequently authorized diagnostic run is recorded below.

## Findings

Live Iris status identifies the expected firmware ELF
`d172e99ba688ed9f313b9655df67b13f0c4f5ca94da37fd8f65b8daa49d26fa2`,
boot `16556209682536332364`. At about 496 seconds uptime, logs show active musical
playback, peak PCM 6,725 (20.5% full scale), nonzero samples, PA requested on, and
no partial writes, output errors or estimated deadline misses. The screenshot
confirms the volume slider at the top. These are digital-path observations;
they do not measure GPIO voltage, codec output, amplifier power or acoustic SPL.

The official [V1.2 hardware guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp-mosaico/user_guide.html)
and [CoreBoard schematic, page 4](https://dl.espressif.com/AE/Mosaico/SCH_SCH_ESP-Mosaico_CoreBoard_V1_2_1_2026-09-24.pdf)
were checked, including visual inspection of the PDF. MCU GPIO52 connects to
codec SDIN, GPIO40 to codec ASDOUT, GPIO45 to NS4150B CTRL. This matches the pinned
BSP and application. The schematic labels directions from the codec side;
changing pins based only on the words DIN/DOUT would be incorrect. R28/R29 are
100 kOhm; the schematic states PA gain = 240k/RIN. No hardware fault is concluded
from this drawing. External occupancy of shared right-side pins remains unknown.

The original read-only telemetry could not expose the codec's actual volume
and mute registers, which motivated the opt-in diagnostic build.

## Opt-in diagnostic build

`CONFIG_ECHOY_AUDIO_DIAGNOSTICS=y` enables private ESP-Iris service **0xECA0**.
Normal defaults leave it off; the normal ELF was checked to contain neither the
RPC handler nor register-readback function. The UI and musical engine are unchanged.

| Method | Empty payload operation |
|---|---|
| 1 | Read cached status; no I2C or speaker action from the RPC thread |
| 2 | Queue exactly one fixed reference tone and codec readback |
| 3 | Cancel a queued/running reference |

The test pauses music without erasing the current in-memory rhythm. It does not
resume music afterward. The audio owner reads codec registers, sends one second
of 1 kHz PCM (peak 8,192, about -12 dBFS, 20 ms ramps), then a 30 ms silent tail.
It reads registers again before muting/flushing queued output. The reference
bypasses musical/UI digital gain to provide a known signal level; moving any UI
control cancels it. It never plays on startup. Firmware installation still
restarts the device and its ordinary in-memory session.

Status `phase`: 0 idle, 1 queued, 2 playing, 3 done, 4 cancelled, 5 failed.
Before sending method 2, read method 1. After its accepted acknowledgement, poll
method 1 until `generation == requested_generation` for the new generation and
the phase is terminal. Do not mistake an older cached Done for the new test, or
replay a command after an uncertain transport result.

The `before.regs` and `after.regs` arrays are ordered:
`[0x00 reset, 0x01 clock, 0x09 serial input, 0x31 mute, 0x32 volume, 0xFD ID1, 0xFE ID2]`.
`valid` is a bit mask: 127 means all seven reads succeeded. A failed read retains
its actual error and only the preceding valid fields. `at_us` timestamps the
capture. At the configured curve/gain, the pinned driver is expected to program
volume register 0x32 to approximately 188 (0xBC); this is an expectation to check,
not an observed hardware value. Mute uses register 0x31 bits 0x60. The before
capture intentionally follows a mute/flush; the after capture precedes final mute.
`pa_requested` remains a software control flag, not a measured electrical signal.

Example authorized calls through the official workspace CLI:

```sh
python3 ../esp-mosaico-vibe/mosaico.py --workspace . --json iris rpc 0xECA0 1 --project projects/echoy --device-id 4553502d49524953010030eda0f452a0
# Method 2 requires explicit permission for the reference playback.
```

## Local verification

- `sh tests/audio/run_probe.sh`: fixed frequency/level, ramps, duration, silent
  tail and arbitrary-block continuity passed. The initial zero-output stub failed
  the expected nonzero-level check before implementation.
- `sh tests/board_audio/run.sh`: 22,278 assertions passed, including real-return
  propagation and partial valid masks; ASan/UBSan/default LeakSanitizer passed.
- `python3 tests/device_runtime/test_runtime.py`: 20 cases passed normally and
  with ASan/UBSan/default LeakSanitizer. New coverage includes no automatic tone,
  duplicate rejection, preserved seed, no automatic replay, queued/active UI
  cancellation, explicit stop, read failure and partial output failure.
- `sh tests/audio_rpc/run.sh`: registration rollback, empty payload enforcement,
  bounded JSON, admission failure and valid generation/readback data passed. A
  response-too-small regression first exposed command admission before a failed
  reply; commands now verify room for a fixed acknowledgement before queueing.
- Official diagnostic and normal diagnostic-disabled ESP32-S31 builds passed.
  Diagnostic build directory: `output/device-audio-probe-build`; SDK defaults
  append `tests/device_runtime/sdkconfig.diagnostics`. Only this option differs
  from the normal configuration; manual acceptance and GSP depth 64 remain.
- Official bundle inspection passed with the same UI hash, partition targets
  and no Recovery/bootloader component. See [artifact manifest](artifacts.json).

## Authorized reference result (2026-10-10)

The user authorized installation and one reference. Operation
`88113a40-7d5d-4c46-892e-4bfde6dac9f2` succeeded, with HEALTHY on boot
`16152412761377796384` and matching ELF SHA-256
`87626318ac1b6071a8b1b1985de555adfe3042347bd71ea8312ee3a059aab071`.
Bootloader and partition hashes remained unchanged. The local submission marker
was written before method 2, and that method was submitted exactly once.

Generation 1 completed with phase 3, error 0, 24,000 tone frames, peak PCM 8,192
and 21,999 nonzero samples. Both readback masks were 127. Register arrays were:

| Capture | Reset | Clock | Serial input | Mute | Volume | Chip IDs |
|---|---:|---:|---:|---:|---:|---|
| Before | 0x80 | 0x3F | 0x0C | 0x60 | 0xBC | 0x83 / 0x11 |
| After | 0x80 | 0x3F | 0x0C | 0x00 | 0xBC | 0x83 / 0x11 |

The user reported the tone was clear and distinctly louder than musical phrases.
This establishes audible output at the known reference level and confirms the
intended codec programming; it does not measure acoustic SPL or frequency response.
No microphone capture was made. [Raw test result](reference-result.json).

## Device output calibration

The approved HTML and shared C synth normalize loops to RMS <= .045, followed by
the shared .45 master gain. At UI volume 1, 144 representative cases (four seeds,
four roles, three phrases and densities 0/.5/1) measured RMS .015064637–.02025 and
peak <= .2925. This is about 19–21 dB below the sustained reference's RMS, despite
correct codec programming. The measurements explain why hardware volume changes
alone were insufficient on this device. [Per-case levels](levels.csv).

`echoy_device_pcm_convert` adds a fixed device-only gain of 3 (+9.54 dB) before
16-bit conversion. Shared synthesis, normalization, note timing, PC mixer, UI
and reference HTML are unchanged. No equalization, compression or per-phrase
gain pumping is added. Existing saturation protection remains and any clipped
sample is counted; logs are split so clipping/errors do not disappear through
the Iris line-length limit. Diagnostic reference tones bypass this calibration.

`sh tests/audio/run_device_pcm.sh` first failed the expected linear-gain check
against the uncalibrated conversion, then passed gain, headroom, invalid-value
and explicit saturation tests. The actual synth/converter test passed all 144
loop cases and 2,304 phase alignments with ten audition taps per second. Highest
calibrated loop peak was .8775; highest tested mixture peak was .919789312, with
zero clipping. This is representative/stress coverage, not an exhaustive claim
over all possible event histories. [Test output](gain-checks.txt).

The 20 runtime cases and ASan/UBSan/default LeakSanitizer passed with the converter
integrated. Official `system-update-bundle` and bundle inspection passed, with the
same UI/partition hashes. [Calibration package](gain3-artifacts.json)
`3f156df66112` retains hardware volume 90, disables the temporary diagnostic RPC,
and was explicitly authorized and installed on 2026-10-10. Operation
`eedc87f8-21cb-4100-b412-7f263e4f06e2` succeeded with HEALTHY on boot
`1222412418091812715`; the expected ELF hash, commit record, unchanged bootloader
and partition hashes all matched. The actual screenshot shows the normal home
screen. Startup telemetry confirms `pcm_gain_milli=3000`, zero clipping, output
errors and estimated deadline misses while idle. Subsequent physical touches
recorded seed `2050` and started playback. At 153,602 ms uptime: 1,373,268 nonzero
frames, peak PCM 14,414, PA requested on, clipping 0, partial writes 0, output
errors 0 and estimated deadline misses 0. The same boot remained active.

The user reported louder output but still too quiet. After checking the default
65% slider position, the user explicitly moved it to maximum and again reported
insufficient loudness. The [actual screenshot](gain3-max-volume.png) confirms the
slider at the top. Acoustic acceptance therefore remains **failed**, despite
healthy digital streaming. No additional tone or package was submitted.
A subsequent role preparation took 42,284,433 us; the existing slow synthesis
issue remains, separate from the output-gain correction.
[Installation result](gain3-device-result.json).

Startup still emits existing SDK messages for an already-installed GPIO ISR
service and disabling a not-yet-enabled I2S channel; codec opening and application
startup then succeed. No change to those initialization paths was made here.
