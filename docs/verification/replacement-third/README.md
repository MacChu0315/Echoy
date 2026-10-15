# Replacement third-device baseline (2026-10-15)

The user replaced f40cd8, then explicitly approved accepted Echoy package
`2b984e511b98` on replacement `4553502d49524953010030eda0f45936` and confirmed
three devices separated >=15 cm on a nonmetal surface. No old-target diagnostic
approval or magnetic fault was assigned to the replacement.

[Delivery evidence](device-result.json), [real home](installed-home.png),
[startup magnetic/audio trace](startup-magnetic-audio.log).

## Exact artifact and operation

Use the preserved accepted package/source/configuration record in
`../cooperation-revision/accepted-baseline.json`. Its immutable `.irisfw` digest
was reverified; no new application build or sound/UI change occurred here.
Official `mosaico.py iris list` discovery plus `iris device-status --endpoint`
performed a fresh handshake: MAC `30:ed:a0:f4:59:36`, normal jump_jump 0.1.0,
Recovery ABI 1, retained layout. The port name is not device identity.

`python3 output/replacement-third-20261015/install.py` uses official
`iris test enter-recovery` followed by exactly one `iris system-update` for this
verified ID. Existing Recovery 0.1.4 is observed ready with crash count zero;
no ROM restore is needed or performed. Operation
`d17dd45f-b043-40a4-a344-f82c3517dafd` is succeeded/HEALTHY/validated, application
boot `10218426682111976170`, expected ELF
`5a721bd70413aabd5716c646b5fd1d29091cffe6464e44c7d994e2923b5d5e95`.
The bootloader flash-region hash is unchanged before/after; target partition
hash matches the accepted package. Component inspection excludes Recovery and
bootloader writes and retains separately packaged UI resources.

## New checks and limits

`python3 output/replacement-third-20261015/post-check.py replacement` runs
identity, current-boot logs and actual screenshot checks through official
`mosaico.py`. Calibration is ready at 3483 ms, right-Y -1/right-Z 205/eight
samples, without rejection or disabled linkage. Accepted role gains and drive
remain present. Startup is silent; partial writes, write errors, deadline misses,
clipping and app crashes are zero. Existing low-level GPIO/I2S initialization
messages remain in the complete local logs, separate from output counters.

Final `mosaico.py iris device-status` queries on all three verified IDs show
normal mode, the same accepted ELF and zero crashes. We do not infer current
magnetic readiness of another boot from a cached disconnected screen; the new
replacement readiness is tied to its observed application boot. Source/model/
PCM regressions belong to the preserved accepted package's earlier verification,
not a new software test run in this installation-only turn.

The replacement's initial hardware delivery/startup checks pass. Its listening
and paired interaction acceptance, three-board linking, acoustic phase bounds
and sustained RF/load are not claimed. The replaced f40cd8's diagnosis remains
unresolved and its diagnostic candidate was never installed.

The user confirms only same-orientation horizontal A—B—C scope. The proposed
three-device design is `../../plans/2026-10-15-three-device-horizontal-design.md`;
architecture implementation and a new three-device rollout require their
respective concrete review/authorization. Current running code remains the
accepted two-device implementation. No first/second write or remote write.
