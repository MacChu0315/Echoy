# Optional Harmony 500 Hz bass-cut trial

The user selected the stronger trial after reporting distortion both on the
desk and while holding the left tile, especially in lower sounds. Approval and
constraints are in `../../plans/2026-10-14-harmony-bass-trial.md`.

The default-off `CONFIG_ECHOY_HARMONY_BASS_TRIAL` selects only new Harmony FULL
banks during the existing preparation worker. A pure-C 500 Hz high-pass follows
normalization and precedes the unchanged role trim. There is no compensation
gain. Source/foreground banks, other roles, timing, plan metadata, UI and final
PCM processing remain unchanged. No microphone is involved.

The filter validates immutable input and an entire periodic output before
writing. Tested musical banks, including the captured phrase, exactly match
the independently prepared 22-period candidate. For a float rounding cycle,
the full-period difference must meet both relative and absolute error limits;
invalid/over-budget input remains byte-identical. See
`../../../tests/harmony_voicing/README.md` for the numeric derivation and limits.
Jobs cannot be modified after calibration or handoff, filtered twice or partly
published after failure. Old audio remains until the existing common-bar commit;
stale jobs and paused updates retain their previous semantics.

## Verification

- `sh tests/harmony_voicing/run.sh`: independently calculated response/phase,
  zero/DC, boundary impulses, periodic reference, atomic rejection, no allocation
  and all twelve expressions pass. ASan/UBSan/LSan pass; host O2 stack is 176 B.
- `sh tests/band/run.sh`: original band/calibration/upstream tests and five new
  groups pass. They cover FULL-only changes, job phase guards, immutable old
  banks until a common bar, Pause, stale/cancelled/failed jobs and failure in a
  later bank preventing an earlier processed bank from publishing. Sanitizers
  pass with leak checks retained.
- `python3 -m unittest discover -s tests/device_runtime -v`: 43 methods pass;
  original lifecycle cases run in ordinary, measurement and bass-trial builds.
  Filter work is confined to the preparation task, failure keeps valid old
  sound, and ordinary builds do not call it. ASan/UBSan/LSan pass.
- `sh tests/audio/run.sh` and `sh tests/audio/run_device_pcm.sh`: unchanged
  synthesis/PCM regressions pass (225028 behavior checks, 60 error/ownership
  checks, 144 loops and 2304 tap mixtures).
- The final production helper and unchanged PCM reproduce all 24 reference
  files exactly across the captured phrase at volumes .65 and 1.0, including
  filtered banks, input, drive, limiter gain and PCM. See `digital-comparison.json`.
  At .65, 120–300 Hz band RMS falls 9.448 dB, total PCM RMS falls 1.956 dB,
  peak remains .599994 and the loop-boundary jump is one signed-16-bit step.
  These are digital metrics, not measured acoustic improvement.
- The configured pinned-SDK `system-update-bundle` target builds both the final
  trial and ordinary configurations. First dependency build was
  `20261014-112414-system-update-bundle-2`; final incremental logs are
  `output/harmony-bass-build.log` and `output/measurement-final-normal-build.log`.
  The retained unused factory-size warning remains; package inspection confirms
  the existing OTA/UI offsets and unchanged partition table, with no bootloader.
  Frozen v4 files and UI sources were not changed.

## Device state

Only left device `4553502d49524953010030eda0f452a0` received `abcdec27b6fe` via
the official workflow after the user paused/separated both tiles. Exact ELF,
healthy/validated state, zero crashes, silent startup and successful magnetic
calibration are verified. Boot ID is `8257068652410574677`; see `package.json`
and `physical-install.json`. Microphone and reference-tone diagnostics are off.
The right remains on ordinary package `09275b935ad8` as a reference.

Focused listening of the new left build is pending. The user was asked to
select Harmony/Arpeggio, record a similar rhythm at default volume, and judge
the recorded reply and two playback loops. Do not mark the acoustic problem
fixed before that feedback. The preserved pre-trial source is included here;
the later empty left snapshot was a different power-on boot with no crash
report, not a post-listening capture of that same phrase.
