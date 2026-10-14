# Harmony bass-cut listening trial

The user approved the stronger low-frequency reduction on 2026-10-14 after
reporting audible distortion both on the desk and while holding the left tile,
especially in lower sounds. The earlier two microphone attempts are exhausted
and were contaminated by office speech. This trial does not use a microphone.

Status: implemented and verified locally; left-only package `abcdec27b6fe` is
installed and its silent startup/calibration are confirmed. Focused listening
is pending. See `../verification/harmony-bass/README.md`.

Only the left device will receive the first listening build. Preserve rhythm,
pitch, expression rules, other roles, UI layout/controls, manual brightness,
recording timing, independent play/pause and cooperative commit semantics.

Apply a fixed second-order 500 Hz Butterworth high-pass to the prepared Harmony
FULL loop at 24 kHz, after existing RMS normalization and before the existing
role gain. Do not renormalize or compensate its loss. Existing final 120 Hz
filter, gain 18 and .60 peak ceiling remain in place. Warm the new filter to
the loop's periodic boundary before producing immutable output. Do all work
in the existing preparation worker, with no live PCM or UI work added.

Keep the trial opt-in through `CONFIG_ECHOY_HARMONY_BASS_TRIAL`, default off.
Source/foreground loops and all other role banks must remain byte-identical.
A prepared job cannot be filtered twice or modified once staged/calibrated.
Failed/stale preparation must not replace a valid current sound; paused tiles
remain silent. Native/reference synthesis without the device trial is unchanged.

The local comparison used the captured pre-listening left Harmony/Arpeggio
plan (`3095f5b66309f7fe4e1f668582d096dc79db4f5256ba1aae1570e20be81df112`).
At UI volume .65, extra 500 Hz filtering reduces 120–300 Hz PCM-band RMS by
9.45 dB and overall PCM RMS by 1.96 dB; peak stays .60. Extra 300 Hz filtering
reduces that band by only 3.19 dB and overall RMS by .48 dB. The user selected
the stronger trial, accepting thinner bass. These are digital calculations,
not measured loudness or proof of an acoustic fix. Results and equal-RMS WAVs
are in `output/harmony-listening-20261014/eq-candidates/`.

Validate the periodic filter against an independent long-warm reference,
invalid-input atomicity, frequency response and loop seams. Validate prepared
bank ownership, stale/cancelled jobs, common-bar replacement and paused updates;
build the ordinary and trial configurations with the pinned SDK before install.
Actual acoustic improvement requires the user's focused listening.

During the post-listening snapshot the left had a new power-on Boot ID and an
empty phrase. Crash query reported zero crashes and no dump. Whether the user
operated the power control is still awaiting clarification. The pre-listening
phrase and exact C replay are preserved; do not silently treat the empty
post-restart snapshot as the phrase the user heard.
