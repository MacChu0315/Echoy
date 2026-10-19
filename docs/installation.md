# Installing and using Echoy

[Project home](../README.md) · [Release identity and limitations](releases/v1.1.0.md)

## Before installing

Download `echoy-v1.1.0.irisfw`, `SHA256SUMS.txt` and the license materials from the
[v1.1.0 release](https://github.com/MacChu0315/Echoy/releases/tag/v1.1.0).
Use the same package on every cooperating tile.

The bundle is for **ESP-Mosaico, ESP32-S31, 16 MiB Flash** with the retained
Mosaico Recovery/Vibe Mode layout `mosaico-retained-recovery-2m-v1`. A compatible
bootloader, Recovery and official host tools must already be available. This
package contains no bootloader or Recovery replacement. Follow the official
Mosaico provisioning guide for blank or unverified devices; do not use arbitrary
application offsets or raw flashing commands.

Updates restart the application and replace the installed app. Recorded phrases
and role selections are session state and reset on restart.

## Verify and install

1. Download the firmware and checksums from the same release. The expected
   firmware SHA-256 is:

   ```text
   ac6401462aed62fb492561a0400ccd9e49c31b48fa13a886b8553b7fb2345c3a
   ```

   On Linux, `sha256sum echoy-v1.1.0.irisfw` prints the value for comparison;
   on macOS use `shasum -a 256 echoy-v1.1.0.irisfw`. If all listed release assets
   are downloaded, run `sha256sum -c SHA256SUMS.txt`. Checksums verify bytes,
   not publisher signatures.

2. v1.1.0 uses shared read-only Flash calibration and supports startup while
   tiles are joined. It does **not** require separated startup calibration.
   For cooperation, arrange at most three tiles horizontally, facing the same
   direction, and wait for contact readiness.

3. In an initialized `esp-mosaico-vibe` workspace, use the official tools to
   verify each live device identity. Install the existing bundle with its
   absolute path and the verified ID:

   ```sh
   python mosaico.py iris system-update \
     --bundle /absolute/path/echoy-v1.1.0.irisfw \
     --device-id YOUR_VERIFIED_DEVICE_ID
   ```

   Replace the placeholder ID; select each attached device explicitly. Check
   your installed CLI's `iris system-update --help`. This workflow preserves
   the retained Recovery and bootloader.

4. Confirm successful completion, healthy application startup and software
   version **1.1.0** in About this tile. If the operation times out, inspect its
   status and the live device before retrying. Do not infer success solely from
   the home screen.

## Local v1.2.0 package

The prepared local `echoy-v1.2.0.irisfw` adds phrase pickup and the battery/slider
fixes described in [v1.2.0](releases/v1.2.0.md). It is not yet a GitHub download;
the published v1.1.0 links and checksum above remain valid for that older package.

For the local package, verify SHA-256
`a0dfee5977225c7c9c868facc47a2d2582ce03022b77fcb02abc007c90e9c39f`
and use the same official installation workflow with its absolute filename.
All cooperating devices must use the same package; About should then show
**1.2.0**. Keep its `SHA256SUMS.txt`, `LICENSE`, `THIRD_PARTY_NOTICES.md` and
`echoy-v1.2.0-licenses.zip`. Recovery requirements and session-reset behavior are
unchanged. Do not substitute the v1.1.0 checksum for the v1.2.0 file.

## Basic operation

- Tap the center to audition the selected role. Slow taps give longer fragments;
  fast taps give shorter grains. Audition accepts at most ten inputs per second.
  After a role change, wait for its image to appear before auditioning it.
- To record a rhythm, tap Record, wait for three one-second counts, then tap
  during four beats. Recording captures touch timing, not microphone audio;
  audition limits do not filter recorded taps. Initial rests are preserved.
- The recorded phrase replies once, then stops. With peers paused, the recording
  tile may reply before the group's background synchronization finishes.
- Add a same-orientation tile on the right and let it receive/prepare the phrase.
  Joining does not start continuous playback. Use Play/Pause on each tile.
- Detaching retains the phrase and local playback permission for that session.
  Restarting clears phrases and resets roles.

## Older versions and limitations

The [historical v1.0.0 package](https://github.com/MacChu0315/Echoy/releases/tag/echoy-three-basic-ba951963754a)
requires tiles to be separated by at least15cm on a nonmetal surface before
startup calibration. This requirement does not apply to v1.1.0. Do not mix
firmware versions in one cooperating group.

See the [v1.1.0 verification scope](releases/v1.1.0.md#validation). Targeted
following, reorder and audition tests do not establish exhaustive RF/load or
acoustic qualification. Keep the distributed license materials with firmware
redistributions.
