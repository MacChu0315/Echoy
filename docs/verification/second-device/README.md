# Second physical device baseline (2026-10-12)

The user authorized replacing `fruit_slice` on the second Mosaico with Echoy,
including subsequent dual-device test updates. Its verified Device ID is
`4553502d49524953010030eda0f40c6c`, initially `/dev/ttyACM1`, with live USB endpoint
`usb:location=1-8.2:1.0`. Select by verified identity, not a remembered tty name.

The exact previously validated package `1d57c5719989` was installed, matching
the first board's current firmware. No application code or audio settings changed.
The source/simulator/build verification remains in
[`../default-expressions/README.md`](../default-expressions/README.md); those
tests were not rerun for this unchanged binary.

## Installation and verification

- Recomputed the complete package SHA-256, then used the offline official
  `esp_iris.py bundle --json inspect` command to check the retained layout and
  component offsets. The bundle contains only the application, UI resources and
  partition table; it does not rewrite the bootloader or Recovery image.
- Official `mosaico.py iris status --all`, `iris list --details` and live
  `iris device-status --project projects/echoy --device-id ...` established the
  second board's identity and idle operation queue before the update.
- `iris test enter-recovery`, followed by live status and boot-specific logs,
  established a ready Recovery writer. One `iris system-update --bundle ...
  --device-id ... --endpoint ...` completed successfully, operation
  `33f48138-97df-4354-9a6c-2073e6cded8f`.
- The installed application is HEALTHY, boot `2867820668781932022`. Its ELF hash
  matches the expected package and the first board. The bootloader hash is
  unchanged; the former game's partition table was replaced by the package's
  verified Echoy layout through the official system-update workflow.
- `iris screenshot` captured the actual native Record-only home, which was
  visually inspected. `iris device-status` and `iris logs --snapshot` confirmed
  the same application boot at 87.5 seconds, crash count zero. Idle PCM statistics
  show zero partial writes, write errors or estimated deadline misses; no sound
  was triggered during this check.
- The original board remained on boot `5127593241566643399`, with the same ELF
  identity and crash count zero. It was not updated or restarted.

See [structured evidence](device-result.json) and [actual home](installed-home.png).
Full local command results are under `output/second-device-install/`; the system
update's authoritative log path is included in the evidence JSON.

## Remaining limits

Before writing, retained Recovery 0.1.2 reset during initialization: failed boot
`13859294364908742442`, reset reason 5 (`ESP_RST_INT_WDT`), address `0x20000`, image
`54f72372a819e6708e859e79cfe2540e51653e5d8ac1c143a357419e43039b22`.
No valid Core Dump was available. The helper preserved crash metadata and waited
for Recovery boot `13827914561527048311` to finish initialization before the
single update. This reproduces the retained Recovery startup issue already seen
on the first board; it is not a crash of the new Echoy application.

Startup logs also retain the existing GPIO ISR already-installed and I2S
disable-before-enabled messages (both present on the first board's same image),
plus the display driver's full-frame scheduling warning. The application
continued to HEALTHY and the home rendered; these low-level messages were not
fixed in this installation task. This is not a claim of warning-free logs or
complete display timing acceptance.

Both devices still run the single-device controller. Hardware neighbor discovery,
musical-state transport and playback clock synchronization are not yet integrated
into Echoy. The pinned BSP has `mosaico_network` topology/contact/message APIs;
their availability is not evidence of Echoy wireless acceptance. Second-board
touch, recording/playback and listening still need physical acceptance.
