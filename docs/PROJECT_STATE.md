# Echoy current state

The approved source remains `reference/echoy-arrange-B-v3.html`; its hash and the
supplied verification document are unchanged. Preserve the design, musical rules
and explicit per-device playback permission. Dependency pins remain in
`dependencies.json`; no remote write or physical device write has occurred.

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
