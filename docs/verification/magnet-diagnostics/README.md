# Third-device startup magnetic diagnostic candidate

Third device `4553502d49524953010030eda0f40cd8` still rejects three LEFT-edge
startup windows after user-confirmed relocation, bare placement and one normal
restart. Existing firmware exposes only averaged right-Y/span/edge masks. The
BSP and fixed-point BMM150 source show that LEFT requires left-X saturation and
that -32768 can be an overflow output with a successful register read. These are
source facts; the exact third-device left-X and cause have not been measured.

`CONFIG_ECHOY_MAGNET_DIAGNOSTICS=y` adds one link-worker warning per rejected
startup window, at most three: both successful compensated X/Y/Z outputs and
exact INT16_MIN sentinel bits. It does not read sensors again, change hardware
settings/thresholds/retries, unpause a peer, alter UI or run on the PCM owner.
It is default-off; separate diagnostic defaults retain accepted office voicing.

[Candidate package](package.json): `2cfd9f7c2bbd`, ELF
`965c61cd405307627f45aba82842cd690f099739b21a93715774089b1b6bd644`.
Immutable artifact: `output/dual-device/echoy-magnet-diagnostics-2cfd9f7c2bbd.irisfw`.
Only this candidate is prepared locally; no diagnostic write has occurred.
Exact third-only update approval is pending. Keep the current bare/separated
position when collecting startup data. The saved two-device tag/package and
accepted sound baseline remain intact; neither earlier device needs this trial.

Verification:

- `sh tests/magnet_diagnostics/run.sh`: enabled/disabled modes pass; the enabled
  assertion first fails before logging exists. Correct sides/values/sentinel
  annotations, cadence, closed failure and invalid-read suppression are covered.
- Same script with ASan/UBSan/LeakSanitizer outside ptrace passes.
- `sh tests/board_link/run.sh`: adapter/transport-window checks pass.
- `sh tests/cooperation/run_transactions.sh`: 26 real two-owner/PCM cases,
  36,969,333 assertions pass; `run_deferred.sh`: 24 cases/420,054 assertions pass.
- `python3 output/third-magnet-diagnostics-20261014/build.py`: pinned SDK and
  GSP/GSPC bundle build succeeds with the known unused-factory-size warning.
  Actual artifact is in `output/device-magnet-diagnostics-build/`; the helper's
  summary names its default older build artifact, so validation uses the actual
  requested build directory, image descriptor, ELF, config and manifest.
- Generated config differs from the accepted build only in the magnet diagnostic
  flag. Microphone/reference diagnostics stay off. Accepted sound/band/PCM/device
  and UI/arrangement sources are byte-identical; packaged UI data is identical.
  Partition offsets/hash remain accepted; application 2,122,784 bytes fits OTA.
- `git diff --check`: clean. No new production dependency or persistent format.

Root cause remains open. Do not interpret a normal screenshot or successful
installation as magnetic calibration readiness. This package adds observation,
not a repair or a three-board implementation.
