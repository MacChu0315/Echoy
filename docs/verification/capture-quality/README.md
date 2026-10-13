# Recording response and manual brightness (2026-10-13)

The user requested removal of idle dimming, smoother rendering and continued
investigation of Harmony distortion. They clarified that suspected missed taps
occur after the screen has entered recording, rather than during count-in.
Physical input reliability and sound quality remain open until board evidence
and listening checks below are completed.

## Changes and limits

- Brightness remains at the explicit 20–100% selection (default 100%). The
  Display page has only its existing manual slider and a matching explanation;
  its idle-dim switch and obsolete captions are removed. Idle time, touching the
  screen and starting playback cannot change the chosen level. Settings remain
  per running tile. This is an explicit user-approved departure from the frozen
  HTML; the reference itself is unchanged.
- Unchanged pointer samples no longer force a redraw of the home art. Knob
  feedback and newly recorded grid positions still update immediately. Pointer
  callbacks still defer gallery image encoding to later timer callbacks.
- Unit-scale coverage avoids duplicate distance calculations, transparent pixel
  rejection avoids unnecessary square roots, and Voice caches its pitch waves
  per existing PSRAM art workspace. Rendering retains pixel coverage, geometry,
  all role animations and bottom control sizes. The workspace grows 7,896 bytes.
- Optional device touch instrumentation wraps each original physical driver
  read/get callback once, without extra controller reads or ACKs. IRQ records
  use internal RAM; detailed task records use bounded PSRAM storage. Successful
  Record arms capture; finishing or cancelling freezes evidence until the next
  Record. Product UI does not print a diagnostic dump. Readers cannot block
  the audio owner or delay input while waiting for a trace lock.
- Private read-only ECA1 methods export a coherent complete ECHP plan with exact
  target volume, and a bounded touch summary. Host scripts use only the official
  CLI, check device/boot identity, reject partial data and preserve existing
  captures. Large storage is explicitly PSRAM. The legacy tone RPC stays disabled.

Neither instrumentation nor pixel tests establishes a physical missed-touch
rate. An accepted UI command is not proof the asynchronous model committed it;
raw matching is not a hardware touch identity. Overwrite/omission/busy counters
must be considered, and repeated taps in the same 156250 us musical grid
intentionally produce one seed bit. Recording duration, countdown, musical
rules, continuous-play permission and sound parameters are unchanged.

## Local verification

- `cmake -S tests -B /tmp/echoy-tests-no-dim`, build and CTest: 2/2 model/display
  tests. The old idle-dim implementation fails the new minute-idle assertion.
- `python3 tools/check_ui_responsiveness.py --build-dir
  projects/echoy/build-sim-capture-quality` (also `--sanitize`): 10 scheduling
  and redraw cases, 12 expression image checks, 10 ownership and 16 construction
  cases. The old forced-redraw implementation fails `idle-pointer-art`.
- `sh tests/art/run.sh` and `sh tests/art_image/run.sh`: 1,722 art checks,
  9,408,768 blend probes, 2,458 image checks and 809 decode fixtures.
- Independent old-renderer comparison: 3,712 frames / 99,716,368 pixels, zero
  differences. Includes nine sizes, all MIDI pitches, role alternation and
  recording layers. Normal and ASan/UBSan/float-cast-overflow checks pass with
  default leak detection outside the ptrace runner. Details remain in
  `output/input-frame-profile-20261013/compare-{final,sanitized}.txt`.
- Official GSP 1.5.1 / GSPC 0.6.1 native build:
  `projects/echoy/build-sim-capture-quality`. `check_full_ui.py --duration-ms
  80000` passes five groups; `check_home.py --duration-ms 70000` passes ten
  recording/hold/cancel/pause/control groups. Chinese and English Display
  screenshots were inspected for clipping and obsolete controls.
- `python3 tests/display/check_idle.py --build-dir
  projects/echoy/build-sim-capture-quality --output <new-directory>` passes
  actual 61.013 s idle and 61.015 s playback at 60%, neutral touch without
  brightness/music change, then manual restoration to 100%.
- `python3 -m unittest discover -s tests/device_runtime -v`: 27/27.
- `sh tests/touch_trace/run.sh` (also with `ECHOY_TOUCH_TRACE_SANITIZE=1`):
  exact forwarding, ring limits, freeze/rearm, 20,000 concurrent reads,
  nonblocking controls and deterministic cross-reset read/get regressions.
  Normal and ASan/UBSan/LSan checks pass, as does the pinned target compile.
- `sh tests/snapshot_rpc/run.sh` and Python discovery in `tests/snapshot_rpc`:
  immutable plans, capacity/CRC/token/lifecycle/PSRAM/overlap checks and 11 host
  export cases. Normal and ASan/UBSan/LSan runs pass. Read-only RPC and touch
  instrumentation received separate code reviews.
- Frozen `reference/v4/SHA256SUMS`, Python syntax and `git diff --check` pass.

PC Voice home timing was 1.197 ms before / 0.941 ms after on the paired corpus;
this is not board FPS. Earlier stable device logs on package `150bd97add2b`
showed Pulse 12.8–13.3 FPS with 80–90 ms UI gaps and Voice 5.6 FPS with 180–190 ms
gaps. Those are playing-home windows, not isolated 2.5-second recording windows.

## Device progress

Current package `73058c98b7ce` is installed on both devices, ELF
`6c2c176f5115b473a5abe9ab36549be7f64640a050d94a54b6c4378fe548bfdb`.
Both identity/HEALTHY/retained-layout/home/calibration checks pass. Exact boots,
operations and idle heap observations are in [device-startup.json](device-startup.json).
The corrected left trace returns a coherent inactive state with all zero
counters before recording. The user still perceived missed responses. Their
first captured rhythm had an unknown physical count: five observed presses
were all stored (`seed=2894`), with a maximum read interval of 226.183 ms.
A follow-up explicitly counted six taps: all six were observed and stored
(`seed=9294`), with no diagnostic loss/overwrites or same-grid merges. The maximum
read interval was still 149.755 ms, versus a maximum read duration of .742 ms
and raw-to-observer delivery of .174 ms. [Complete evidence](physical-input.json).
This establishes preservation of that six-tap sequence; it does not clear faster
taps or the perceived response delay. Current sparse IRQ timestamps are not
physical press identities and cannot independently establish touch latency.
New isolated board FPS and acoustic acceptance remain pending. Local
UI/recording/manual brightness results are
in [ui-checks.json](ui-checks.json), [home-checks.json](home-checks.json), and
[brightness-checks.json](brightness-checks.json); approved Display pages are
[Chinese](display-zh.png) / [English](display-en.png).

The first candidate `606c0563f4e0` was installed on the left, operation
`ffa21ce7-ac9a-4c8f-b17f-0f1f5d37b1cb`, HEALTHY boot `3749417238833005809`, ELF
`e52a189be6123f92ab403bf94f7a046bc9ace1cdebc362ea26e9fdc9ccfe4735`.
Identity, retained bootloader/layout, home and separated magnetic calibration
pass. Live methods 1/2 exported the initial empty plan with exact volume .65,
without changing its silent idle state.

During that installation, independent review reproduced an important diagnostic
race: a deferred reset could let a read retain old IRQ sequence numbers with a
new recording generation, causing unsigned counter wrap. This affects trace
statistics, not forwarded controller data. The candidate's trace is not used
as evidence and it is not being installed on the right. The corrected reset
epoch has a deterministic regression that fails on the old version; independent
replay now yields zero false IRQs and one explicit dropped diagnostic record.
Actual driver results are unchanged.

The pinned SDK build passes. Its only warning is the existing unused factory
partition size check; the inspected bundle targets OTA at 2162688 and contains
only the retained partition table, application and UI resource components.
The built standalone bootloader file is an older unused October 9 output and
is not part of this bundle; retained bootloader identity is checked using device
inventories. The official `iris memory` request returned HTTP 501 (task memory
observation unavailable in this configuration), rather than a device crash;
the same Boot ID remained healthy. A dedicated Iris service stack watermark
has not been obtained. Host stack checks and successful live methods 1/2 are
partial evidence, not a target stack-depth measurement.

Harmony production audio remains unchanged. The exact linked .65-volume device
sample and the user's clean pre/post-limiter WAV comparison are now recorded
in `../harmony-stages/README.md`. A same-phrase clean device level is being
measured before choosing a new acoustic trial.
