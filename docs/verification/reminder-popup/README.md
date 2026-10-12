# Larger, automatically dismissed reminder

After experiencing `86406d956f52`, the user confirmed the reminder appears but
found its bottom text too small. At their request the same reminder is now a
300x128 native GSP panel over the center pad (x90, y127). The original wording is
split into two lines, using existing 22px medium Chinese and 18px bold English
font packs. The former inline reminder was 13px Chinese.

The third-tap trigger, four-second lifetime, once-per-run policy and dismissal
on Record/phrase acquisition are unchanged. No scrim, close button or input
handler is added: the pad continues producing sound through the panel and the
bottom Record button is unobscured. The normal small status line remains intact.
No audio profile, musical/capture state or persistence changes were made.

## Verification

- The new native assertion failed on the preceding build because its third tap
  showed only small status text (`output/reminder-popup/red.log`).
- Official GSP 1.5.1 / GSPC 0.6.1 builds of `echoy_backend` and `echoy_desk` pass.
  The scene still uses 32 font packs; no new font dependency or size family was
  introduced. [Artifacts](artifacts.json).
- `python3 tools/check_home.py --build-dir projects/echoy/build-sim-reminder-popup
  --duration-ms 60000 --output output/reminder-popup/home` passes, as does the
  `--reduced-motion` variant with a separate output. Checks cover the first two
  taps without a popup, the third-tap panel, both languages, no redraw occlusion
  during further taps, expiry without extending it, no repeat, and dismissal by
  Record/Cancel. Recording, one-shot, Play/Pause and retained-rhythm checks pass.
  [Normal](home-checks.json), [reduced motion](home-reduced-checks.json).
- `python3 tools/check_desk.py --build-dir projects/echoy/build-sim-reminder-popup
  --output output/reminder-popup/desk` passes. After the third tap's sound tail
  finishes, a fresh tap through the still-visible popup produces nonzero actual
  C PCM. All linked three-tile playback, preparation, detach, language, export,
  restore and quiet-tail checks pass. [Results](desk-checks.json).
- `sh tests/v4_strip/run.sh`: permissions/motion and 40 glyph RGBA cases pass.
- `cmake --build build-host` and `ctest --test-dir build-host --output-on-failure`:
  2/2 recording/display tests pass.
- Pinned SDK low-noise `run_build_step` with
  `-B output/device-audio-ui-build system-update-bundle`: exit 0, warnings 0,
  run `20261012-100753-system-update-bundle-2`. The actual app is 1,567,888 bytes;
  inspected package `a859d454627f` preserves the retained layout and includes the
  app, partition table and updated UI resources. The runner summary's older
  default build path is not used as artifact evidence.

Native Chinese/English screenshots were visually reviewed; the popup is legible,
the Record control is unobscured, and the panel remains above animated pad art.
No persistent desktop was started for this iteration. To preview locally:

```sh
python3 tools/desk_preview.py --build-dir projects/echoy/build-sim-reminder-popup \
  --no-build --no-browser --port 18902 --visual-ports 3240,3241,3242
```

## Physical installation (2026-10-12)

The follow-up package `a859d454627f` is installed on the same verified Mosaico
`4553502d49524953010030eda0f452a0`, using the official update workflow and the
user's ongoing same-device update authorization. Operation
`a0daa59b-0a52-4577-8b40-eb147e3be642` succeeded from Recovery boot
`6479646384073322565` to HEALTHY application boot `11406880910400298043`.
There was no extra Recovery reset during this follow-up. Image identity,
healthy/commit evidence and unchanged bootloader/partition-table hashes pass.
[Device evidence](device-result.json), [actual home](installed-home.png).

`iris device-status`, `iris screenshot` and `iris logs --snapshot` confirm normal
mode on the same new boot after one minute, crash count zero, ready Record-only
controls and no UI/audio write/deadline errors. Musical gains and PCM gain are
unchanged. The previously observed GPIO ISR/I2S initialization messages still
appear; they did not prevent healthy startup. The popup was left unused for the
user to trigger with three physical taps; physical readability acceptance is pending.
