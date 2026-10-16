# Echoy for Mosaico

Tap a rhythm. Connect another tile. Build a small ensemble.

Echoy turns touch-recorded rhythms into musical phrases on ESP-Mosaico. Each tile can use Pulse, Voice, Dust or Harmony, and decides independently when to play or pause. Recording captures **touch timing, not microphone audio**.

**敲出节奏，水平拼接，让不同声部回应彼此。**

[Download v1.0.0](https://github.com/MacChu0315/Echoy/releases/tag/echoy-three-basic-ba951963754a) · [Install](docs/installation.md) · [Develop](docs/development.md) · [Mosaico Skills](https://github.com/MacChu0315/Echoy/tree/skills/mosaico-development)

<p>
  <img src="docs/screenshots/v1.0.0-connected.png" width="280" alt="Echoy device screenshot: left tile connected with a recorded phrase">
  <img src="docs/screenshots/v1.0.0-following.png" width="280" alt="Echoy device screenshot: right tile following the left tile, with its own Play button">
</p>

Actual screenshots from the v1.0.0 package's successful **two-device** check: connected on the left, following on the right. These are device screenshots, not photographs or proof of all three-device transitions. [Screenshot provenance](docs/screenshots/README.md).

## Try it

1. Keep the tiles separated during startup calibration, then wait for the home screen and successful sensor readiness.
2. On one tile, tap **Record**, wait for **3, 2, 1**, then tap a rhythm during the four recording beats. The reply plays once and stops.
3. Join another tile on the right, with both tiles facing the same direction. The right tile follows the left phrase; joining does not start continuous playback.
4. Select a role and tap **Play** on each tile you want to hear. Pause affects that tile only. Detaching retains the current phrase and its playing/paused permission in the running session.

The published three-device package supports same-orientation horizontal chains of up to three tiles. Restarting does not restore your recorded phrase or role choices. See [installation and usage](docs/installation.md).

## Download and version boundaries

The download is **`echoy-v1.0.0.irisfw`**, an IRIS system-update bundle with application, partition information and UI resources. Its internal version is `1.0.0`; the original package identity remains `ba951963754a…`. Use the official Mosaico update workflow, not a guessed raw-flash address.

| Version / branch | What it preserves | Status |
|---|---|---|
| [`echoy-dual-accepted-20261014`](https://github.com/MacChu0315/Echoy/tree/echoy-dual-accepted-20261014) | Dual-device source at `148080a9`; package `2b984e511b98` | User accepted the seven-category physical checklist and retained sound |
| [`echoy-three-basic-ba951963754a`](https://github.com/MacChu0315/Echoy/tree/echoy-three-basic-ba951963754a) | Source for the v1.0.0 download at `a83a64e` | Basic three-device operation confirmed; live rearrangement remains faulty |
| `master` | Accepted dual-device code with updated public documentation | **Not the source of the three-device download** |
| [`archive/echoy-three-device`](https://github.com/MacChu0315/Echoy/tree/archive/echoy-three-device) | Later three-device investigations and changes | Archived experimental work; not the recommended download |

### Known limitations of v1.0.0

- Moving A from the left of **A—B—C** to the right to form **B—C—A** can leave A independent instead of following C. Reattaching A did not reliably resolve the observed case.
- Startup still performs magnetic calibration. Separate devices by at least **15 cm** on a nonmetal surface before powering on or restarting; do not start this version already joined.
- Basic three-device use was confirmed, but the full three-device checklist, sustained wireless load and measured acoustic synchronization were not fully accepted. No vertical, L-shaped or rotated arrangement is supported by the published scope.

Version `1.0.0` follows the existing firmware metadata; it is **not a claim that all three-device behavior is stable**. [Release details and checksums](docs/releases/v1.0.0.md).

## Development and reusable skills

The application uses shared C musical logic and native ESP-GSP UI. [Development instructions](docs/development.md) cover the pinned environment, audible desktop simulator, build and regression commands. The desktop simulation and physical hardware have different acceptance boundaries.

Eight reusable skills live on the separate [`skills/mosaico-development`](https://github.com/MacChu0315/Echoy/tree/skills/mosaico-development) branch: five engineering guides and three experimental experience/design guides. Their checks are not product acceptance.

## License

Echoy's original code and documentation are licensed under [MIT](LICENSE), copyright © 2026 MacChu0315. This grant also covers the author's original contributions in the two product tags listed above; the tags themselves have not been rewritten. Third-party code, fonts and SDK components retain their own licenses.

The bundled ESP-GSP component uses **Espressif Modified MIT**, restricted to use with Espressif products. The whole firmware is not uniformly MIT-licensed. Preserve the [third-party notices](THIRD_PARTY_NOTICES.md) and the license materials supplied with the download.

## Maintenance

Echoy is preserved as a working experiment. No regular updates or ongoing support are currently planned. The accepted dual-device baseline and the known-limited three-device package remain separately identified for reference and reuse.
