# Bounded right-device output measurement

The user approved the clarified onboard-microphone trial on 2026-10-14:
`../../plans/2026-10-13-output-measurement.md`. This is a temporary diagnostic,
not a change to Echoy's touch recording or production sound profile.

## Implementation

`CONFIG_ECHOY_OUTPUT_MEASUREMENT` defaults off and is mutually exclusive with
the earlier ECA0 tone diagnostic. ECA2 registration does not start sampling;
all four methods must register before any handler accepts requests. A failed
registration remains inert even if public RPC unregistration is busy.

An explicit start pauses music and hands the existing sole audio owner a fixed
24 kHz mono16 sequence: 2 s quiet, 15*(350 ms tone + 200 ms quiet), 0.25 s quiet.
Five frequencies (220/330/440/660/1000 Hz) each use peaks .12/.24/.36 and 20 ms
ramps. Input gain stays fixed; hardware output remains 90. The sequence has
252000 frames (10.5 seconds), with a 12-second absolute deadline. The amplifier
is enabled into zero PCM at explicit diagnostic open, before the settling
interval. Ordinary audio open remains muted. No input is fed
back to output. Partial I/O, RX overflow or deadline expiry stops the run.

RX closes before the existing low-priority preparation worker analyzes and
erases raw PCM. Owner cache publication never waits for readers. Complete,
cancelled and failed runs never resume music automatically. The screen shows
measurement status and the existing full-size Pause button; other controls
cannot change the stimulus. A delayed richness drag cannot cancel a new run.

Runtime admission allows two starts per boot, including runtime recreation.
The host tool additionally reserves at most two attempts across reboots in
`output/harmony-measurement-20261014/consent-ledger.json` before submitting a
start. Uncertain or failed submissions consume an attempt and are never
retried automatically. It verifies the fixed right ID, exact diagnostic ELF,
normal mode, board/layout and empty operation queue. Collection uses sixteen
compact metric RPCs and checks boot/run before publishing local results. It
does not export raw microphone waveforms or use remote services.

## Local verification

- `sh tests/output_measurement/run.sh` and `--sanitize`: exact stimulus length,
  independent frequency/harmonic references, arbitrary chunk boundaries,
  ramps, noise/DC/headroom, whole-capture clipping, missing frames and cancel.
  ASan/UBSan/LSan pass. Host stack checks: render 80 B, analysis 336 B.
- `sh tests/board_audio/run.sh`: ordinary 22267 checks, final diagnostic 95699 checks,
  three separate retained-cleanup-failure cases of 1487 checks each. Same suite
  passes ASan/UBSan/LSan. Ordinary opens remain TX-only; disabled APIs refuse
  capture. See `tests/board_audio/README.md` for public SDK error handling.
- `python3 -m unittest discover -s tests/device_runtime -v`: 39 test methods
  pass, retaining the previous 27 and including partial/error subcases, two-run
  admission, cancellation generations, cache contention, worker handoff,
  shutdown retention/erasure and a separately compiled ordinary build.
  `ECHOY_RUNTIME_SANITIZE=1` passes with ASan/UBSan/LSan.
- `sh tests/measurement_rpc/run.sh`: request/capacity/gain/run guards, compact
  JSON, PSRAM results, registration rollback failure and no implicit capture.
  `MEASUREMENT_RPC_CFLAGS` sanitizer variant passes.
- `python3 tests/measurement_rpc/test_host.py`: 20 tests pass, including fixed
  identity/ELF/mode/queue, durable budget across reboots, no start retry,
  damaged/missing ledger, concurrent admission and incomplete/stale collection.
- `python3 tools/check_ui_responsiveness.py --build-dir
  projects/echoy/build-sim-capture-quality`: eleven scheduling cases including
  pending richness at measurement start, plus existing QOI, ownership,
  creation and real home-worker cases. Normal and sanitizer runs pass.
- Official GSP 1.5.1 with the production C UI and controlled measurement
  snapshots: visible capture/analysis captions, locked controls and Pause
  restore the normal strip. These fixtures do not open a microphone.
- Ordinary native GSP checks: `tools/check_full_ui.py` passes five groups;
  `tools/check_home.py --duration-ms 65000` passes ten input/recording groups;
  `tools/check_cooperation_ui.py --unit-only` passes fifty caption/visibility
  assertions, all glyphs fit, still 32 font packs. The first home check ended
  at its default 30-second backend lifetime before its full sequence finished;
  logs show clean UI shutdown, and the complete finite run passes.

Sanitizer runs retain leak checking. They run outside the environment's ptrace
restriction; a restricted-run LSan fatal error is not counted as a pass.

Pinned-SDK ordinary and diagnostic builds passed in runs
`20261014-102123-system-update-bundle-2` and
`20261014-102134-system-update-bundle-2`. The ten-handler diagnostic replacement
was built with the same configured SDK and verified by
`cmake --build output/device-measurement-build --target system-update-bundle -j 4`
(exit 0). Its low-noise IDF wrapper had finished producing the bundle but stayed
in its event loop with no child process; it was interrupted only after the
direct target passed and both bundle hashes matched. That wrapper run
`20261014-102600-system-update-bundle-2` is recorded as failed, not a passing run.
The one build warning remains the unused factory partition size check; the
inspected bundle targets the existing OTA partition, UI data and unchanged
partition table, and contains no bootloader.
The final settling revision built successfully through that same CMake target
using the pinned runner's `idf_command` environment. A bare-shell recompile
failed because `esp-idf-configdep` was absent from its PATH; no source check was
disabled. `output/measurement-settled-build.log` records the successful target.
The final ordinary configuration also builds successfully with measurement
disabled and the original eight RPC slots (`output/measurement-final-normal-build.log`).
Final digital-only checks `sh tests/audio/run_device_pcm.sh` and
`sh tests/audio/run.sh` pass (144 loops, 2304 tap mixtures, 225028 behavior and
60 error/ownership checks). They validate numerical bounds and scheduling,
not acoustic freedom from distortion. Codec gain configuration was compared
with the pinned BSP; no proven configuration mismatch was found.

## Physical observations and limits

The first diagnostic package `03e899870629` installed normally, preserved
Recovery/layout, calibrated and stayed silent, but ECA2 registration returned
`ESP_ERR_NO_MEM`. Inspection of the pinned service implementation established
that its eight-entry method table was full: Recovery uses three, snapshots
three, and measurement needs four. No capture was started or attempt consumed.
The diagnostic-only configuration now reserves ten entries and `main.c` rejects
an undersized table at compile time. Ordinary configuration remains unchanged.
The original ordinary package `09275b935ad8` is retained for restore.

Replacement `885acd881c85` registered all methods, calibrated and initially
reported idle, zero captured frames and two remaining starts. Run 1 used its
original 0.5 s lead / 9 s profile at fixed input gain 0 dB. It completed 216000
TX and RX frames in 9044084 us, with no error or ADC clipping; see `run-1.json`.
Only the two louder 1000 Hz windows passed the conservative signal/noise guard.
The first quiet-window AC RMS was .00069956, and the first 220 Hz window peak
was .0198 versus .0009/.0015 at subsequent higher levels. Startup/enable
transients are a plausible confound, not a proven cause of the user's Harmony
distortion. The user did not notice or could not judge the test tones.

For the second and final authorized attempt, the diagnostic changed only its
measurement preparation: enable PA into silence, allow two seconds to settle,
measure noise from 1.2 to 1.8 s, and use fixed input gain 12 dB. Tone levels and
the 12-second deadline remain unchanged. The first run and consent ledger are
preserved; the host explicitly recognizes both sequence profiles and rejects
mixed frame/window metadata. Package `379a099a0803` completed 252000 TX/RX frames
in 10543422 us, with no I/O error, ADC clipping or near-clipping. Five windows
passed the numerical signal/noise guard; see `run-2.json`.

After both runs the user clarified that the office had ongoing speech, they
were not focused on the sounds, and they could not leave. The local quiet
windows do not prove absence of speech during the tone windows. Consequently,
even guard-passing ratios are **not controlled acoustic evidence** and must
not be used to assign a hardware fault or choose production EQ/gain. No speech
waveform was exported; raw PCM was erased after analysis. The consent ledger
contains two reserved attempts and must never be reset to enable another run.

Right device was restored via the official workflow to ordinary package
`09275b935ad8`, retaining Recovery/layout, with crash count zero and silent
output. Its ELF identity matches the previously accepted firmware and has no
ECA2 microphone diagnostic entry point. The left device was not reflashed.
The right's restore boot initially rejected bottom-edge magnetic field 0x04
(right_y 256–258, span 2–4) three times and disabled physical link sensing.
The user then separated and restarted the right tile. New Boot ID
`13916864028309266146` reports the exact ordinary ELF, zero crashes, and
`magnetometer baseline ready; horizontal contact enabled` at 2115 ms. Startup
calibration is confirmed; no new linked playback or listening trial was run.
See `physical-session.json` for identities and restore/recalibration provenance.

The result measures the combined speaker, placement and microphone path.
It is not calibrated SPL or proof of a defective analog component. ADC clipping,
near-clipping or low fundamental/noise ratio invalidates harmonic comparisons.
Central 200 ms analysis windows tolerate a fixed TX/RX offset up to 55 ms;
RX overflow is rejected rather than interpreted as distortion. Host fixtures
cannot establish real DMA timing or acoustic headroom. The sole owner enforces
the deadline through bounded I/O; there is no independent hardware watchdog
guarantee against an entirely stalled processor.
