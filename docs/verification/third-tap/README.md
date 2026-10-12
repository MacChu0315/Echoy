# Reminder after three trial taps

The user chose a reminder after the third tap in the trial pad, before clicking
Record. The same status line displays “点录制，再敲一遍，就能留下节奏。” for four
seconds; English displays “Tap Record, then tap again to keep your rhythm.”
The wording makes clear that recording starts a fresh performance; it does not
claim to have saved the taps already played.

Each running tile counts successful new pad presses only while no phrase exists
and recording is available. Long holds do not add taps. Taps during recording,
following, faults or preparation do not consume this guidance. The reminder
appears once per run, is not extended by further taps, and ends when Record starts
or a real phrase exists. A cancelled recording does not re-arm it. No new modal,
automatic recording, playback permission, musical data or persistence was added.

The implementation is local UI state in `echoy_ui.c`; the generator extends the
existing status glyph corpus. Font sources, sizes, layout and audio code remain
unchanged. Updated generated subsets contain the additional Chinese characters.

## Preview

Open <http://127.0.0.1:3237/> and tap the center three times. A was left empty with
its reminder unused. Desktop: <http://127.0.0.1:18901/>. Earlier previews remain
alive; do not rebuild their live bundles.

```sh
python3 tools/desk_preview.py --build-dir projects/echoy/build-sim-third-tap \
  --no-build --no-browser --port 18901 --visual-ports 3237,3238,3239
```

## Verification

- The added native check failed on the preceding record-only build specifically
  because the third tap did not change the status line (`output/third-tap/red.log`).
- Official GSP 1.5.1 / GSPC 0.6.1 builds of `echoy_backend` and `echoy_desk` pass
  without warnings. The scene remains within the 32-font-pack runtime limit.
- `python3 tools/check_home.py --build-dir projects/echoy/build-sim-third-tap
  --duration-ms 60000 --output output/third-tap/home` passes; the same command
  with `--reduced-motion` and its separate output also passes. Coverage includes:
  two taps without prompting, held-press counting, third-tap prompting, English
  localization without restarting the timer, expiry after four seconds despite
  further taps, no repeated prompt, and immediate dismissal on Record/Cancel.
  The existing empty/successful recording and Play/Pause tests also pass.
  [Normal](home-checks.json), [reduced motion](home-reduced-checks.json).
- `python3 tools/check_desk.py --build-dir projects/echoy/build-sim-third-tap
  --output output/third-tap/desk`: passes the third-tap prompt with real shared C
  audio/controller state and verifies seed, event count and playback permission
  remain empty. Existing three-tile playback/linkage/restore/export checks pass;
  mixed PCM is nonzero when expected and silent after pause, and owners close
  without errors. [Results](desk-checks.json).
- `sh tests/v4_strip/run.sh`: permissions, motion, stale cancellation, boundaries
  and all 40 RGBA glyph cases pass.
- `cmake --build build-host` and `ctest --test-dir build-host --output-on-failure`:
  2/2 recording/display tests pass.
- Pinned IDF low-noise `run_build_step` with
  `-B output/device-audio-ui-build build`: exit 0, warnings 0; run
  `20261010-194037-build-2`. [Actual build artifacts](artifacts.json) are recorded
  separately because the runner's summary points at the older default directory.

The Chinese/English prompt and its disappearance were visually inspected in
native screenshots. Package `86406d956f52` was installed on hardware on 2026-10-12 as recorded below.

## Physical installation (2026-10-12)

The user explicitly requested the existing four-second reminder wording and the
Record-only empty home. The pinned SDK `system-update-bundle` build passes with
zero warnings, run `20261012-095108-system-update-bundle-2`; its application hash
matches the previously verified artifact exactly.

Official `mosaico.py iris system-update --bundle ... --device-id ... --endpoint ...`
installed package `86406d956f52` on the same Mosaico, operation
`ca0930e9-fa6c-4215-8045-99b05123c247`. New HEALTHY boot
`9542740490602219176` has the expected ELF identity and unchanged bootloader and
partition table. `iris device-status`, `iris screenshot` and `iris logs --snapshot`
confirm normal mode, crash count zero, the [single Record home](installed-home.png),
no UI errors and unchanged speaker gain/profile. [Device evidence](device-result.json).

Before any package write, retained Vibe Mode 0.1.2 reset during startup: failed
boot `14436840074278624180`, reset reason 5 (`ESP_RST_INT_WDT`), Recovery firmware
address `0x20000`. It returned on boot `4761115984407552929` and completed
`app_main`. The original helper stopped before submitting any update. Logs and
the official crash report were preserved; no valid Core Dump existed. The explicit
save-core request returned HTTP 404 for the absent dump, despite the wrapper
classifying it as `device_unavailable`; this was not evidence of another reboot.
After verifying the stable Recovery boot and empty operation queue, one update
was submitted and succeeded. This old Recovery startup issue is not fixed here.

The user confirmed that the reminder appeared on the physical device, but found
the bottom text too small. They requested a larger automatically dismissed popup;
that follow-up was subsequently implemented and installed as `a859d454627f` (see
[popup verification](../reminder-popup/README.md)). The wording and audio parameters
remain unchanged.
