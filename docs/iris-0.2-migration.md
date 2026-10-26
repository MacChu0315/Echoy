# Iris 0.2 migration candidate

Version **1.2.1 is an Iris 0.2 migration candidate** with single-device acceptance,
a bounded muted-loop soak and user-completed dual-device testing. Platform review
and physical three-device acceptance remain separate. The accepted v1.2.0 source
tag and firmware remain unchanged.
The candidate moves Echoy onto the Iris 0.2 application/host contract while
preserving the accepted musical and UI behavior.

## Pinned environment

Use the exact revisions in [dependencies.json](dependencies.json). This set is
from the official workspace's `feat/v26.0.3` integration branch, pinned by commit;
it is not a floating branch dependency. ESP-IDF, ESP-GSP 1.5.1, GSPC 0.6.1,
LVGL 9.5.0 and the accepted office voicing profile remain pinned.

For coexistence with an accepted 0.1 installation, this branch expects the
matching workspace at `../esp-mosaico-vibe-iris02`. Its utils and BSP directories
are independent checkouts, not replacements for `../esp-mosaico-vibe`.
The application CMake/component manifest, CLI configuration and host test scripts
all use that same dependency set. Configure a fresh build directory; do not reuse
an old generated `sdkconfig`. The workspace configuration now uses schema 2.

The ordinary `mosaico.py` launcher and offline bundle inspector must come from
that 0.2 checkout. The custom Echoy ECA1 plan snapshot schema remains 1: its data
format is independent of Iris wire version 2.

## Compatibility changes

| Contract | Accepted v1.2.0 | Migration candidate |
| --- | --- | --- |
| Iris bundle schema | `esp-iris-system-update/v1` | `esp-iris-system-update/0.2` |
| Product | `esp-mosaico/v1` | `esp-mosaico/v2` |
| Recovery ABI | 1 | 2 |
| Retained partition | `factory`, app/factory | `vibe_mode`, app/test |
| Application partition | `ota_0`, app/ota_0 | `main_app`, app/ota_0 |
| USB CDC | One Iris interface | Separate control and data interfaces |

The partition addresses/sizes for application, NVS, coredump, sysmeta and UI
resources are retained. The normal update bundle includes only the partition
table, application and UI resources. It does not carry a bootloader or Vibe Mode
image. Relabeling the old archive cannot migrate its application protocol.

The matched BSP updates sensor driver packages and display DMA handling. It
explicitly retains integer BMM150 samples and the overflow sentinel used by the
fixed Flash classifier. The calibration profile and application relation rules
are unchanged. Host simulation cannot establish physical sensor or display timing.

## Local verification

Build with the pinned low-noise IDF runner and a fresh build directory. Include
`sdkconfig.office-voicing-trial.defaults`; retain pickup and disable microphone
measurement. The build target is `system-update-bundle`. Use the pinned 0.2
host Python for `ESP_IRIS_PYTHON` and for the artifact checker:

```sh
"$IRIS_PYTHON" tools/check_iris_bundle.py \
  --utils ../esp-mosaico-vibe-iris02/submodule/esp-mosaico-utils \
  --build-dir output/iris02/build-dynamic-audio \
  --bundle output/iris02/build-dynamic-audio/echoy-system-update.irisfw
```

The checker uses the official Iris parser and verifies actual application/ELF
identity, archive bytes, UI image, partition table, protocol configuration,
voicing and bilingual About defaults. Run the pickup/PCM, group, runtime,
BSP-adapter and native UI/three-owner checks described in [development](development.md).
Original migration results: the pinned firmware build and offline bundle checker passed; all
16 targeted regression command groups pass, including 54 device-runtime methods.
The measurement host has 20 passing methods and UI responsiveness has 60 passing
groups. A direct accepted-v1.2.0 comparison covers 144 office-voiced pickup cases
and 8,640,000 byte-identical PCM samples. The official native three-owner check
passes 10/10 when run separately from compilation, with zero late blocks, zero
clipping and exact paused silence. That original candidate left application C
source unchanged; the device-driven startup corrections below are subsequent.

Original, failed-on-device candidate archive SHA-256:
`dade481bea0f688d5550d6404d4073f8bb8b5eb2be500939253ee04555213dd8`.
The build retains upstream SDK include-validation warnings, an optional IDF
extension trust warning, and the size warning for the smaller retained Vibe Mode
slot; the application targets the verified larger `main_app` slot. No trust
override or partition-size workaround is enabled.

These are local checks; platform upload acceptance, physical listening and
physical three-device behavior remain separate gates.

## Device-driven startup corrections

The original candidate could stall when main-task brightness control overlapped
the first LCD transfer. Initial brightness is now set before GSP starts, with
successful values cached to avoid a duplicate main-task write. Manual changes
continue on the serialized UI callback path. One PSRAM DMA draw buffer preserves
internal DMA memory for Wi-Fi initialization. IRAM-safe I2S interrupts preserve
audio service while the host verifies Flash inventory after HEALTHY; audio
deadlines and queue sizes are not relaxed.

Echoy also explicitly disables its unused motor at app entry, before slow or
fallible initialization. A motor shutdown failure retains Iris access but stops
further application startup. The user confirmed that this replacement stops the
continuous vibration observed with the earlier startup-only correction.

The PCM owner also runs at priority 6 on core 1, above the pinned Iris/TinyUSB
workers at 5 and GSP at 4. Recording/role-switch failures retained the same Boot
ID: DMA interrupts continued every approximately 5 ms, but the owner did not
consume completed buffers in time. Equal-priority scheduling is now excluded
by the task priority, compile-time checks and a task-creation regression.
The single-device recheck included recording/playback and 20 subsequent plan
updates without a write error or deadline miss; the user observed no protection
page in that run. This is not long-duration or multi-device qualification.

Power-on testing subsequently found a distinct LCD transport allocation failure.
PSRAM rendering scratch did not bound the pinned BSP's internal DMA bounce
buffers: its ten queued transfers could retain far more internal memory than
the application could spare. The application's public panel-IO constructor
wrapper now caps only this QSPI LCD to two queued transfers, approximately
16 KiB of staging payload plus overhead. BSP/SDK code and transport flags are
unchanged. Host isolation/error and archive-order checks pass, and the ELF call
path was verified. Five software restarts had normal app state and no observed
LCD errors. The user confirmed two physical power-on starts reached the home
screen and responded to touch; the final cold boot also passed live log checks.
The Iris shadow screenshot alone must not be treated as panel acceptance.

The fixed 1.2.1 candidate was freshly built with the pinned dependencies and
passed the official bundle checker and single-device healthy installation.
Six startup lifecycle tests, 55 device-runtime tests and the board-audio checks
pass. The bundle checker rejects the old configuration without IRAM-safe I2S.
Temporary diagnostic RPCs, allocation probes and I2S timing callbacks are excluded
from this package.

LCD-queue rollback archive SHA-256:
`160e253bda0d26b9189cc58e751ec3638cf0a7d102af4ae1603c37bf15326600`.
The local artifact is `output/iris02/artifacts/echoy-v1.2.1-iris02-lcd-queue.irisfw`.

The subsequent fixed-audio candidate embeds build-generated float32 timing/tap
cues and audition pools; recording, gains, randomness and UI remain unchanged.
The original C synthesizer generates the assets, and the actual ESP comparison
passed for all seven buffers with maximum absolute sample error 1.45e-6. Sound
preparation measured about 3.42 seconds in the previous package and 0.132–0.148
seconds in this candidate. This is not the complete power-on duration. It adds
about 1 MB to Flash and PSRAM rodata. The final package excludes the numerical
diagnostic; installation and two software restarts reached normal app state
without observed LCD transfer errors or audio write/deadline failures. The user
confirmed two physical power-on starts and normal four-role audition, recording
and repeated role changes. Long-duration and three-device physical acceptance
remain separate.

Fixed-audio rollback archive SHA-256:
`7fdecb9f0e4f4866d131c5d48454dc68cd4458c9f070fbf3cdd807aa825a7ac6`.
The local artifact is `output/iris02/artifacts/echoy-v1.2.1-iris02-fixed-audio.irisfw`.
The subsequent dynamic-preparation candidate shares anchor-note synthesis
between foreground and FULL without changing either mix's summation order or
normalization, and embeds 36 common triangle tables. It adds 590,352 bytes to
the application, including 589,824 bytes of tables; other valid pitches retain
runtime construction. In twelve same-device paired plans, Harmony preparation
averaged 1.060 to 0.556 seconds, Voice 1.073 to 0.929, Dust 0.361 to 0.324 and
Pulse 0.393 to 0.381. All numerical checks passed (maximum sample error 2.50e-7).
The ordinary image contains no diagnostic/reference code. Installation and two
software restarts passed, and the user accepted the current single-device
experience. No additional instrumented cold-boot run is claimed by this feedback;
three-device physical acceptance remains pending.

An authorized muted continuous-playback soak was then observed for 18 h 55 min
38 s and stopped at the user's request on 2026-10-25. Across 227 status samples
and 2,271 audio-counter records, boot/firmware identity stayed unchanged, crash
count and audio write/deadline errors remained zero, and free PSRAM stayed at
8,722,572 bytes. No collection gap or increased device log-drop counter was
observed. The stopped recorder's evidence is preserved. This is acceptance
evidence for that bounded single-device workload, not multi-device qualification.

Current archive SHA-256:
`a16b35c00491785e517d9b238eaa3fa4973f491dd8aa678bdd17a72963f39e18`.
The local artifact is `output/iris02/artifacts/echoy-v1.2.1-iris02-dynamic-audio.irisfw`.
On 2026-10-26 a second, already ABI2 device received this exact archive after its
previous application's exact restore package was preserved. Both devices reported
the same Echoy ELF, normal state, no crash and zero audio write/deadline errors in
the captured checks. Radio logs showed reciprocal horizontal peers and the same
group graph; the final snapshots showed a paused root and a paused follower with
their revisions committed. The user reported finishing testing and authorized
source push and replacement of the platform firmware attachment. No detailed
manual scenario checklist or acoustic phase measurement is inferred from that
report. Physical three-device acceptance remains pending. Platform attachment
updates and review approval are distinct from device/user acceptance.
The tested device's separate NAND/LittleFS failure was repaired on 2026-10-24
by an explicitly authorized filesystem rebuild using a temporary maintenance
image. A 16 KiB write/read test, post-reboot readback, probe removal and empty
directory check passed. The unchanged Vibe Mode 0.2.0 subsequently mounted NAND
and registered its file service. The then-current Echoy package was restored;
its code does not format NAND. This device-local maintenance is not included
in the release archive and does not repair another device's stored filesystem.
Startup acceptance is not full physical audio, interaction or three-device
acceptance.

```sh
python3 -m unittest discover -s tests/startup -v
python3 -m unittest discover -s tests/device_runtime -v
sh tests/board_audio/run.sh
sh tests/display_io/run.sh
sh tests/audio/run_fixed_audio.sh
sh tests/audio/run_preparation.sh
```

## First device transition — approval required

An Iris 0.2 Gateway cannot update an Iris 0.1 device. Before touching a device,
confirm availability/identity, export the required old logs and state with the old
tools, preserve the accepted package, and release its old Gateway. Session phrases
and role selections are not persistent across reboot.

The matched official `recover` workflow installs the 0.2 base through ROM. The
reviewed base manifest declares these writes (sizes are image sizes, not whole
Flash erase ranges):

| Image | Offset | Bytes |
| --- | --- | ---: |
| Bootloader | `0x2000` | 21,984 |
| Partition table | `0x8000` | 3,072 |
| Initial OTA data | `0x9000` | 8,192 |
| Vibe Mode 0.2.0 | `0x20000` | 1,488,608 |

The planned application update then installs its table at `0x8000`, application
at `0x210000`, and UI resource image at `0xf00000`. Verify exact artifact hashes,
erase alignment and the official write plan again at the time of approval.
Do not erase the whole Flash or overwrite undeclared NVS/sysmeta/coredump regions.
Preserved bytes are not a promise of cross-version interpretation of internal state.

Start with one board. Verify live version/ABI/layout, normal → Vibe Mode → normal,
health, touch/brightness, recording, original sound, pickup and UI synchronization.
Only then migrate the other two and exercise the three-device arrangements.
No device write is included in the local migration work.

If rollback is needed, release the 0.2 Gateway and use the separately preserved
0.1 environment/base plus accepted v1.2.0 application through the official native
recovery/install workflow. This is a full transition, not an in-place Iris update
or guaranteed automatic rollback. Validate the available old base artifacts before
approving device work; exact restoration of an individual board's former base
version requires that particular base image.
