# Echoy current state

The user approved rebuilding from `reference/echoy-arrange-B-v3.html` with
ESP-GSP JSON/resources and shared C UI, simulator comparison, then hardware
integration. The frozen HTML and supplied verification document retain their
original hashes. Never change the baseline to make the implementation pass.

## Implemented

- Official app under `projects/echoy`; dependency pins remain in `dependencies.json`.
  The isolated ESP32-S31 SDK remains at `../.toolchains/esp-idf-7b9cc1a`.
- Real C composition for all four roles / twelve expressions, including anchor,
  support, pitch, duration and event ordering. Native home graphics display the
  committed full plan; recording displays only captured anchors.
- Chinese/English home, role gallery, settings, guide, information and recovery
  pages, with phrase memory, density changes at musical boundaries, one-shot
  audition, explicit looping, scrolling, export and PC remove/restore.
- The approved remove action is hidden on hardware and retained on PC. PC desk
  controls live outside the device screen. They simulate fault/loading/battery
  conditions and download actual generated musical data, not fabricated audio.
- Official native GSP checks and the fixed-SDK build are recorded in
  `verification/README.md`. The two home Canvas callbacks and three gallery QOI
  images share the same renderer and plan data across native/firmware builds.

## Remaining

Audio synthesis and actual output scheduling are not implemented. Visual playback
advances the real C plan but currently produces no sound. Single-tile PC simulation
does not implement multi-device linking, clocks or wireless transport. Battery and
other hardware readings are still mocked; device export has no output adapter.
No physical device writes or hardware acceptance have occurred in this rebuild.

The user's missing reply was an implementation gap: the earlier foundation only
stored/drew raw tap bits. A captured `0x44a0` seed is retained as a deterministic
PC verification fixture. Ordinary taps remain audition-only as required by the
HTML; recording requires Record, two count-in beats and four capture beats.

Next: implement the frozen prototype's audio synthesis/output behavior, then
multi-device and hardware adapters. Continue with simulator evidence before
physical validation, and obtain authorization before device writes.
