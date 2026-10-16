# Installing and using Echoy v1.0.0

[Project home](../README.md) · [Release identity and limitations](releases/v1.0.0.md)

## Before installing

Use the `echoy-v1.0.0.irisfw` asset from the
[v1.0.0 release](https://github.com/MacChu0315/Echoy/releases/tag/echoy-three-basic-ba951963754a),
plus its checksum and license materials. The bundle is for **ESP-Mosaico,
ESP32-S31, 16 MiB Flash**, with the retained Mosaico Recovery/Vibe Mode layout
`mosaico-retained-recovery-2m-v1`. It is not a generic ESP32 firmware image.

A compatible Mosaico bootloader, Recovery and official host tools must already
be available. This package contains no bootloader or Recovery replacement.
The original installations used Recovery 0.1.2 and 0.1.4; this is not a promise
of compatibility with arbitrary hardware or future tool versions. Follow the
official Mosaico provisioning guide for a blank or unverified device instead
of attempting raw application flashing.

Updates restart the application and replace the installed app. Recorded phrases
and role selections are session state, not persistent saved projects.

## Verify and install the bundle

1. Download `SHA256SUMS.txt` and the firmware from the same release. The exact
   bundle identity is recorded in [release details](releases/v1.0.0.md).

   On Linux, run `sha256sum -c SHA256SUMS.txt` in the directory containing all
   listed assets; on macOS, `shasum -a 256 echoy-v1.0.0.irisfw` prints the value
   for comparison. A checksum verifies bytes; it is not a publisher signature.

2. Separate every tile by at least **15 cm**, on a **nonmetal surface**, before
   the update/restart. This older version captures a startup magnetic baseline.
   Do not start with the tiles joined; move nearby magnetic accessories away.

3. Use the official Mosaico tools from an initialized `esp-mosaico-vibe`
   workspace. Verify the live device identity with those tools. With the bundle
   at an absolute local path and the verified ID substituted, the existing-bundle
   update command is:

   ```sh
   python mosaico.py iris system-update \
     --bundle /absolute/path/echoy-v1.0.0.irisfw \
     --device-id YOUR_VERIFIED_DEVICE_ID
   ```

   Do not paste the placeholder ID. With multiple attached devices, select each
   explicitly. Check your installed CLI's `iris system-update --help`; do not
   replace this with an arbitrary `esptool` address or `idf.py flash` command.
   The system-update workflow preserves the retained Recovery and bootloader.

4. Confirm that the operation completed successfully and the intended application
   boots healthy. Keep the tiles separated until magnetic calibration succeeds;
   the home screen alone does not prove calibration readiness. If an update
   times out, inspect the operation and live device state before retrying.

5. Use the **same published package on every cooperating tile**. Do not mix it
   with the dual-device package or later experimental firmware.

## Basic operation

- Choose a role: Pulse, Voice, Dust or Harmony, each with three expressions.
- Tap the center to try a sound. To keep a rhythm, tap Record, wait for the
  three one-second counts, and tap during four recording beats. The initial
  silence is preserved. The result replies once, then stops.
- Join tiles left-to-right with the same orientation. Let the right tile finish
  receiving/preparing its phrase. Joining alone never grants continuous Play.
- Tap Play on each tile you want to hear. Pause controls only that tile.
- During the running session, detaching retains the committed phrase and local
  playing/paused permission. Restarting clears session phrases and resets roles.

## Known issues and acceptance limits

Three-device rearrangement, the older Recovery startup issue and historical
acceptance limits are described in [release details](releases/v1.0.0.md#what-was-accepted-and-what-was-not).
No new device installation was performed for this documentation update.
