# Audible shared-C three-tile prototype

The approved HTML already defines synthesis, A/B/C linkage and playback policy.
Complete these in the native simulator before physical transport/hardware work.
Preserve the frozen design, source algorithms, independent play permission,
boundary handovers, detach retention and per-tile mute/recovery behavior.

Architecture: portable C synthesis renders immutable 24 kHz phrase buffers;
portable C ArrangeModel compiles/stages linked plans. A portable band controller
coordinates capture models, asynchronous preparation and sample-based playback.
A Linux adapter supplies one audio output clock and separate GSP owner threads
for three official simulator instances. OS audio uses installed PulseAudio tools;
no SDK forks or new production packages. The desktop retains the prototype's
outer dragging/snap controls and embeds actual native GSP screens.

Verification: compare pure linkage with unchanged JavaScript, compare synthesized
sound against browser OfflineAudioContext, test scheduling/cancellation/mixing,
exercise all three GSP UIs and linked recording/playback, capture real PCM output,
then build the same portable sources for the pinned ESP32-S31 SDK. Audio rendering
and desktop multi-tile results do not establish wireless/acoustic hardware sync.
The existing user preview remains intact until its replacement is verified.

Completed PC scope: shared C DSP, linked arrangement, sample scheduler, native
three-screen desk, actual PulseAudio output, cross-language/audio oracles, eleven
full integration checks and fixed-SDK compilation. Results and physical adapter
limits are recorded in `docs/verification/audio-linked/README.md`.
