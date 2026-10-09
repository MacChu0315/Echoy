# Quiet audio investigation: evidence before another gain change

Installed baseline remains `d87ae02e9222` (hardware volume 90). The user reports
very quiet sound with the UI slider at maximum, and also with Voice/Dust roles.
The user's board has no known prior acoustic baseline. No new firmware or test
tone has been sent during this investigation.

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

Current read-only firmware telemetry cannot expose the codec's actual volume
and mute registers. The next step is controlled instrumentation rather than
another gain increase or an unapproved change to musical timbre.

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

Package `bb7f0a3dd05f` is local and awaits authorization to install and play one
reference. Until then, codec readback, reference loudness and the root cause of
quiet physical sound remain unverified. No microphone capture is performed.
