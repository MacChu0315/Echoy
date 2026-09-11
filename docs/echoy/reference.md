# Echoy executable reference

The only behavioral and visual source is `reference/Echoy_v5.0.3.html`.
Its filename says 5.0.3, while the embedded page version is 5.0.4. The
accepted bytes have SHA-256
`d23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5`.

## Rebuild and verify

Run these commands from the project root:

```sh
node tools/extract_reference.mjs --self-test
node tests/oracle.mjs --generate
node tools/capture_reference.mjs --generate
node tools/capture_reference.mjs --check
node tests/oracle.mjs --self-test
```

`capture_reference.mjs` launches local Chrome in headless mode. Environments
that restrict browser process sockets must run the two capture commands with
the host's browser permission. Neither command uses the network. `--check`
captures into memory and compares it with the committed metadata and PNGs; it
does not overwrite the reference images.

The final self-test checks the source and module hashes, regenerates all music
and session data in memory, checks every committed byte, validates all PNG
dimensions and hashes, and repeats the in-memory generation to prove that its
output is stable.

## Extracted rules

`tools/extract_reference.mjs` verifies the source hash before writing anything.
It extracts the four product modules using their named comment boundaries:

- `DuijuResponse` to `reference/extracted/response.js`
- `DuijuHarmony` to `reference/extracted/harmony.js`
- `DuijuEngine` to `reference/extracted/engine.js`
- `DuijuSession` to `reference/extracted/session.js`

Each boundary must occur exactly once and in order. The extractor's self-test
proves that missing and duplicate start or end markers are rejected. The
manifest records the source path, source identity, exact extracted hashes and
the three pinned reference-repository commits. The stored HTML and extracted
modules are made read-only after generation; they remain reference inputs and
are not firmware sources.

## Music fixtures

`tests/cases/music-fixtures.json` contains 18,360 cases and 81,120 expected
events. It is compact JSON because it is machine input for the later C oracle
comparison. `tupleFields` and `eventFields` define the array layout.

The cases are the full Cartesian product of:

- four roles and all three phrases;
- activity endpoints 0 and 1, the current role's default, and exact threshold
  samples `.25`, `.26`, `.30`, `.42`, `.52`, `.65`, `.70`, `.72`, `.74`,
  `.75`, `.76`, `.78`, `.80`, and `.82`;
- bars 0 through 4;
- duplicate indices 0, 1, and 2;
- solo, empty motif, all-weak motif, sustained notes, chords, and several
  simultaneous notes at one step.

Outputs preserve every prototype event field, including duration, velocity,
expression, voicing, chord name, articulation, and maximum duration. The
generator also rejects an event outside the 16-step bar, a non-positive
duration, a velocity outside the prototype range, or more than 16 events.

## Session trajectories

`tests/cases/session-trajectories.json` records six executable trajectories
with 21 checkpoints. Each checkpoint contains the injected monotonic time, the
complete product-state snapshot, and the resulting events and motifs. The
trajectories cover:

- discovery without joining;
- stable contact, clock agreement, future-bar join, and no automatic play;
- a local play request waiting until the next beat;
- release removing the response edge at a bar while retaining the shared
  session clock;
- volume zero and output failure retaining a valid motif, followed by a data
  failure invalidating it at a boundary;
- idle role, phrase, memory, and activity commits.

These traces are prototype behavior. The `.38 s`, `.45 s`, `1.05 s`, and
`1.8 s` values remain simulator assumptions and are not hardware measurements.

## UI baselines

`tests/cases/ui-cases.json` defines 19 product-screen states in both `zh-CN`
and `en`, for 38 captures. The set includes all four performance instruments,
playing, beat waiting, pending activity, all four role/phrase selections,
volume, boot, data loading, role preparation, role failure, data failure,
audio failure, and low battery.

Every capture fixes the viewport to 480×480 at device scale 1, injects a
10,000 ms clock, disables timers and animation, enables reduced motion, fixes
the color profile, and injects named Liberation Sans, Noto Sans CJK, and
DejaVu Sans Mono faces. `tests/cases/ui-capture.json` records each font path,
size, and SHA-256 together with the exact Chrome version.

The same capture file records visible text, every product `data-action`, its
value, label and hit rectangle, and selected computed style properties and
rectangles. For canvases it records dimensions, a pixel checksum, the complete
motif for that frame, and the score-step list. The self-test requires the
score-step list to match that motif exactly.

Strict 480×480 PNGs live under `reference/ui/<locale>/`; their hashes and
dimensions are in `reference/ui/manifest.json`. They are browser-reference
inputs for the later LVGL comparison. They do not establish MCU rendering,
touch latency, frame rate, audio, radio synchronization, or physical-device
acceptance.
