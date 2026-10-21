# Iris 0.2 migration candidate

Version **1.2.1 is a local migration candidate**, not a published or physically
accepted release. The accepted v1.2.0 source tag and firmware remain unchanged.
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
  --build-dir output/iris02/build-firmware \
  --bundle output/iris02/build-firmware/echoy-system-update.irisfw
```

The checker uses the official Iris parser and verifies actual application/ELF
identity, archive bytes, UI image, partition table, protocol configuration,
voicing and bilingual About defaults. Run the pickup/PCM, group, runtime,
BSP-adapter and native UI/three-owner checks described in [development](development.md).
Local results: the pinned firmware build and offline bundle checker pass; all
16 targeted regression command groups pass, including 54 device-runtime methods.
The measurement host has 20 passing methods and UI responsiveness has 60 passing
groups. A direct accepted-v1.2.0 comparison covers 144 office-voiced pickup cases
and 8,640,000 byte-identical PCM samples. The official native three-owner check
passes 10/10 when run separately from compilation, with zero late blocks, zero
clipping and exact paused silence. All application C source remains unchanged.

Candidate archive SHA-256:
`dade481bea0f688d5550d6404d4073f8bb8b5eb2be500939253ee04555213dd8`.
The build retains upstream SDK include-validation warnings, an optional IDF
extension trust warning, and the size warning for the smaller retained Vibe Mode
slot; the application targets the verified larger `main_app` slot. No trust
override or partition-size workaround is enabled.

These are local checks; platform upload acceptance, physical listening and
physical three-device behavior remain separate gates.

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
