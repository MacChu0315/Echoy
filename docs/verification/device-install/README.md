# First device installation and battery visibility correction

Target selected and authorized by the user on 2026-10-09: `/dev/ttyACM0`,
ESP32-S31 Mosaico v1.2, Device ID `4553502d49524953010030eda0f452a0`.
The previous normal app was Woofly 1.1.0. Recovery is 0.1.2, ABI 1, product
`esp-mosaico/v1`, layout `mosaico-retained-recovery-2m-v1`.

## Installation evidence

Official commands used `mosaico.py --workspace .` with explicit Device ID:

- `iris device-status --project projects/echoy --endpoint /dev/ttyACM0` verified
  the live target, role and declared compatibility before updating.
- `iris system-update --bundle output/device-release/echoy-single-device-38b79718eed8.irisfw
  --device-id 4553502d49524953010030eda0f452a0 --endpoint /dev/ttyACM0` attempted
  the exact approved artifact, without rebuilding it or using generic flash.
- First operation `40e0e9ec-9330-4a90-980d-f6926121bcf5` ended `outcome_unknown`.
  Read-only `operation-reconcile`, status, logs and crash reports showed an
  interrupt-watchdog reset (reason 5) in factory Recovery at `0x20000`, a new
  Recovery boot, no committed update ID and no valid retained Core Dump.
  Application erase had begun; successful installation was not inferred.
- The first erase started 2.659 seconds into Recovery, while display/network
  initialization was still running. After a HEALTHY event and the completed
  initialization log, a controlled same-bundle retry was submitted from the
  stable Recovery session. No timeout was relaxed and no Recovery image changed.
- Second operation `7d241c9d-8c4b-4a1c-b3ee-fbef614ef87b` succeeded. Application
  and UI readback hashes matched, the existing partition table already matched
  and was not rewritten, and the expected Echoy image booted with HEALTHY.
  The timing difference supports a startup-overlap hypothesis, not a proven
  low-level explanation for the Recovery watchdog.

Successful boot: `909078037626529738`. Expected/observed ELF SHA-256:
`d3330310044e0eacf2c12e9b94a39f1bb254a71c2ebc6b5d5da9640ed89126af`.
Before/after bootloader hash:
`d3cc31fc9e8f3df9c8ca3beece23fbcb97019f59cb87933266f8ddc47efadbcc`.
Before/after partition hash:
`4f8786d12001684a3c02385f31ef167247827bd73b744801494a8c5851bc7231`.
The manifest contained no bootloader or Recovery component.

## Actual application failure

`iris logs --snapshot` and `iris screenshot` exposed:

```text
ECHOY_UI_ERROR: 261 line=426 operation=echoy_view_show((u)->view, ("battery_fill"), (!battery_unknown))
echoy: UI controller failed: 261; Recovery remains active
```

[This screenshot is from the device](first-device-fault.png), not a simulator.
The firmware is installed, but its UI failed initialization and stopped the
audio/controller runtime. Do not call the installed version accepted or usable.
The health task performed the pinned BSP's battery initialization; acoustic,
interactive and steady-state resource acceptance remain outstanding.

The compiled API descriptor for `battery_fill` had `properties: []`. An object
name alone does not provide `set_visible`; the same omission existed in the role,
settings, guide and info headers. Previous normal-72% tests never attempted to
hide these fills, so the view cache bypassed the missing operation.

## Local correction and regression

`tools/generate_scene.py` now assigns a unique `visible` binding to all five
fills and regenerates `projects/echoy/ui/main.json`. No layout, wording, musical
logic, runtime policy, dependency, partition or frozen HTML changed.

The new `tests/device_view` fixture uses the production C UI and official GSP
1.5.1 backend. It supplies health readings only; it does not emulate hardware.
The test reproduced the exact 261 initialization failure before the fix.
After the fix, [four groups pass](health-view-checks.json): unknown startup,
compiled visibility operations, unknown → 37% → unknown text/fill updates, and
navigation through home, role, settings, guide and info. The test drives the pad
through pointer down/up samples, as required by its pointer observer.

[Fixed unknown-battery screenshot](fixed-unknown-simulator.png) is **simulator
evidence only**. The ordinary [8-group home regression](home-checks.json) also
passes. ESP32-S31 build `20261009-121900-build-2` has zero warnings. The official
`system-update-bundle` target and archive inspection succeed.

Reproduce from the repository root:

```sh
cmake -S tests/device_view -B projects/echoy/build-sim-device-health \
  -DCMAKE_BUILD_TYPE=Release \
  -DGSPC_EXECUTABLE=/home/zhuzhengming/esp/ts/Mosaico/.toolchains/gspc-0.6.1/gspc
cmake --build projects/echoy/build-sim-device-health -j 4
python3 tests/device_view/check.py --build-dir projects/echoy/build-sim-device-health --output output/device-health-view-check
```

Use a fresh output directory; the checker snapshots its executable/resources to
avoid changing a live bundle. Local loopback access is required.

Prepared fix:
[`echoy-battery-binding-afae9df52f28.irisfw`](../../../output/device-release/echoy-battery-binding-afae9df52f28.irisfw),
1,090,287 bytes, SHA-256
`afae9df52f2849714d677aeed6c78077d481837dd682dc81808f11bbb5735950`.
[Component manifest](fix-artifacts.json). The user later authorized this package
and it was installed successfully; the visibility-binding error disappeared.
Subsequent audio findings are in [the audio report](../device-audio/README.md).
Installation success must not be substituted for UI or acoustic acceptance.
