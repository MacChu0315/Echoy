# Echoy current state

The approved source is now `reference/v4/echoy-arrange-B-v4.html`; hashes are in
`reference/v4/SHA256SUMS`. The delivered ZIP and v3 historical baseline remain
unchanged. The v4 musical/audio/arrangement code matches v3. Preserve the design, musical rules
and explicit per-device playback permission. Dependency pins remain in
`dependencies.json`; no remote write or physical device write has occurred.

## v4 presentation

On 2026-10-09 the user explicitly accepted the current v4 GSP simulator effect.
Treat this as approval of the simulated presentation and interaction baseline.
It does not authorize device writes or establish physical hardware acceptance.

The single-line identity, raised pad/sliders and 84/244/84 bottom row now follow
v4. Native hit targets switch independently of the visual transition; capture
uses one Cancel target, playback/preview uses one Pause target. Stale Cancel
cannot arm a new recording. A pure C CSS ease-out curve includes shortened
reversal timing, and fixed glyph masks provide alpha/scale without replacing
native controls or dynamic status text.

The user explicitly confirmed touch/mouse acceptance; do not claim native
Tab/Enter focus support. The desk propagates system reduced-motion changes to
all three C views. Linux native loopback ACK hints and the direct host's high
simulation tick remove RPC stalls; hardware uses the shared C view/controller.
Verification: `verification/v4/README.md`. Current live preview uses
`build-sim-v4`; do not rebuild its bundle in place while it is running.

## PC application

The audible three-tile entry is `tools/desk_preview.py` (HTTP 18896, native GSP
views 3222/3223/3224). A starts on the desk; B/C start silent on the shelf.
It runs the real portable C synthesis, ArrangeLinked/ArrangeModel and band
scheduler. Browser code controls placement and embeds official GSP screens;
it does not synthesize sound or substitute a separate musical model.

Audio runs on a shared sample timeline independently of GSP rendering. Each
GSP handle has its own owner thread. Immutable preparation jobs execute outside
the central state lock and stale revisions are discarded. File export/status I/O
also occurs outside the audio lock. Playback output uses installed `paplay`.

Confirmed behavior includes tap audition, recording cues, repeated identical
recordings, one-shot replies, linked full phrases/pitches, independent looping,
boundary changes, exact detach retention, fault recovery and remove/restore.
New neighbors never auto-start, replaced automatic-preview jobs stay cancelled,
and language selection updates all three product screens.

Verification and concrete limits are recorded in `verification/audio-linked/README.md`.
The supplied HTML remains the oracle, including actual Chrome offline sound.
Do not call the earlier Vibe single-screen UI-only entry an audible acceptance
run. It remains useful for isolated UI regressions.

## Remaining physical integration

Portable sources compile for the pinned ESP32-S31 SDK, but the physical app
entry has not connected the speaker/output adapter, neighbor discovery,
communication or clock synchronization. Battery and peripheral readings remain
mocked; device export has no hardware destination. No acoustic, wireless, GPIO,
resource-watermark or physical-device acceptance has occurred.

Next: connect and validate these hardware adapters using the approved design.
Determine the actual neighbor/transport interfaces from the pinned BSP and board
before selecting a physical protocol. Obtain authorization before device writes.
Do not repeat the earlier mistake of treating UI-only or single-tile tests as
completion of the whole musical prototype.
