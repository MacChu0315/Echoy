# Echoy for Mosaico

English · [简体中文](README_CN.md)

Tap a rhythm on Mosaico, connect tiles side by side, and let different musical voices respond to each other.

**[Published v1.0.0 firmware](https://github.com/MacChu0315/Echoy/releases/tag/echoy-three-basic-ba951963754a) · [Installation guide](docs/installation.md) · [Developer guide](docs/development.md)**

<p>
  <img src="docs/screenshots/v1.0.0-connected.png" width="280" alt="Left Mosaico connected with a recorded phrase">
  <img src="docs/screenshots/v1.0.0-following.png" width="280" alt="Right Mosaico following the left tile, with its own Play button">
</p>

Actual device screenshots: a connected pair, both paused. [Image source](docs/screenshots/README.md).

> **v1.1.0 source:** includes three-device reorder fixes, shared Flash calibration, earlier local recording replies and role-aware center audition. Joined startup is supported. The source is tagged for review; the download above remains historical v1.0.0, which requires separated startup calibration. [v1.1.0 details](docs/releases/v1.1.0.md).

## Play in three steps

1. **Record.** Tap Record, wait for **3, 2, 1**, then tap during four beats. Your phrase plays back once and stops. Recording captures touch timing, not microphone audio.
2. **Connect.** Place another tile on the right, facing the same direction. It follows the left phrase; connecting does not automatically start continuous playback.
3. **Make an ensemble.** Choose Pulse, Voice, Dust or Harmony, then tap Play on each tile you want to hear. Each tile has its own Pause. Detaching retains its phrase and playing/paused state for the current session.

Use up to three tiles in a same-orientation horizontal chain. Restarting clears recorded phrases and resets role choices. [More about installation and use](docs/installation.md).

## Which version should I use?

The **v1.1.0** source and package are documented in [release details](docs/releases/v1.1.0.md). A v1.1.0 firmware download has not been published; the existing download is v1.0.0.

This version includes the accepted three-device repair, corrected guide and center-audition work. The old [`echoy-three-basic-ba951963754a`](https://github.com/MacChu0315/Echoy/tree/echoy-three-basic-ba951963754a) and [accepted dual-device](https://github.com/MacChu0315/Echoy/tree/echoy-dual-accepted-20261014) tags remain unchanged. Historical v1.0.0 checksums and limitations are in [its release record](docs/releases/v1.0.0.md).

## For developers

Echoy uses shared C musical logic and native ESP-GSP UI. The [developer guide](docs/development.md) covers environment setup, the audible desktop simulator, builds and tests.

The separate [Mosaico Skills branch](https://github.com/MacChu0315/Echoy/tree/skills/mosaico-development) contains eight reusable engineering and experience/design guides, with their validation limits documented.

## License and maintenance

Original Echoy code and documentation: [MIT](LICENSE), © 2026 MacChu0315. Third-party components retain their own licenses; **ESP-GSP is restricted to Espressif products**. Keep the [third-party notices](THIRD_PARTY_NOTICES.md) and license materials with redistributions.

Echoy is preserved as a working experiment. No regular updates or ongoing support are currently planned.
