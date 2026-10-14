# Third-device baseline installation (2026-10-14)

The user authorized the exact two-step installation and confirmed all three
boards separated >=15 cm on a nonmetal surface. Official `mosaico.py recover
--source reviewed --hardware-mac 30:ed:a0:f4:0c:d8` installed/verified reviewed
Recovery 0.1.4, then one official `iris system-update` installed accepted Echoy
package `2b984e511b98`. No application rebuild, sound change, first/second device
write or three-board implementation occurred.

[Result](device-result.json), [real home](installed-home.png),
[current-boot logs](current-boot.log), [expected hashes](expected-identities.json).

- Verified ID: `4553502d49524953010030eda0f40cd8`, factory MAC
  `30:ed:a0:f4:0c:d8` (initially `/dev/ttyACM2`).
- Recovery operation: `a03669b4-37ca-4666-a231-adc60b8c232b`, version 0.1.4,
  verified boot `16758858234958354640`, ELF
  `4b9c3296d2e218977d0a88d205f69f328ebb0a5867cd3fbde0e15a5fc6afc55b`.
- Echoy operation: `a5593cdd-84f1-471d-85cf-bc2ac56f945d`, HEALTHY/validated boot
  `7287316600049295121`, accepted ELF
  `5a721bd70413aabd5716c646b5fd1d29091cffe6464e44c7d994e2923b5d5e95`.
- Formal identity, image, expected partition migration and retained bootloader
  checks pass. Screenshot is a normal empty Pulse/Accent Record-only home.
  At 162.9 s the same boot has crash count zero; startup PCM, partial writes,
  write errors, clipping and deadline misses remain zero. Retained GPIO/I2S
  initialization messages are not new output faults.

## Verification and outstanding calibration

`recover --source reviewed` passes with the existing pinned `IDF_PATH`.
The four source images match the archived reviewed manifest and the official
ROM writer verifies written hashes. Application package digest matches the
saved accepted baseline. The official system update reports HEALTHY/validated
and unchanged source/target bootloader inventory hash.

The initial verification script compared the 24,432-byte bootloader file hash
to an inventory hash covering 0x2000..0x8000 (24,576 bytes). That assertion fails
because these are different ranges. Reading the public inventory implementation
and hashing the reviewed file plus 144 erased `0xff` bytes yields exactly
`925d2541ed213aa29f552f15ee6881b899dd72c4dbc81343ae2b4c88caa72cc4`, matching
both pre/post application inventories. No firmware retry was performed.
The base partition sector is reviewed partition data padded to 4096 bytes;
Echoy changes it to the same accepted application layout as the first two boards.

`post-check.py third` passes identity, retained layout, actual screenshot and
audio checks, but reports **BASELINE_READY False**. Both BMM150 sensors initialize;
three startup windows reject a stable LEFT-edge classification:
`right_y=-88`, span 2..3, `edges=0x08`. The pinned header defines LEFT as `1U<<3`;
the first user-facing description accidentally called it bottom and was corrected.
The classifier requires an in-range/stable right-Y value AND zero startup edges.
The exact magnetic-environment versus hardware cause is not established.

The application is installed and healthy, but physical linkage is unavailable
until calibration succeeds. Repositioning confirmation is requested before one
normal third-device restart. Do not reflash, relax classifier thresholds or
claim magnetic readiness/third-board cooperation from the normal home screen.
This authorization does not automatically cover later third-device upgrades.

## One relocated/bare-device recheck

The user confirmed repositioning, then confirmed that the third device is bare
and has no external magnetic accessories. One official gateway `device.restart`
operation `d9a0854b-0203-4272-b129-0d1d732831be` succeeds. New normal boot
`13092980898669071311` retains the accepted ELF and zero crashes/audio errors.
At 84.0 s, all three calibration windows still report LEFT `0x08`; right-Y now
approaches zero (previously -88). [Recheck result](recheck-result.json),
[magnetic/audio trace](recheck-magnetic-audio.log), [real home](recheck-home.png).
No additional restart, firmware write or sensor-threshold relaxation followed.

The pinned classifier requires left-X saturation for LEFT. Its BMM150 fixed-point
API may return the -32768 overflow marker while register reading succeeds. The
installed firmware does not expose full axes, so no exact left-X or hardware
cause is claimed. A bounded startup-only readback package is prepared locally
in `../magnet-diagnostics/`; third-only update approval is pending.
