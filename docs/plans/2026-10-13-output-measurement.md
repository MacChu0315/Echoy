# Short device output measurement

Status: approved on 2026-10-14. After clarifying that this uses the right
Mosaico's onboard microphone to observe its own speaker, the user explicitly
agreed. Implementation, local verification and both allowed runs are complete.
The user subsequently reported ongoing office speech and no focused listening;
these are uncontrolled ambient observations, not clean acoustic acceptance.
The two-attempt budget is exhausted. Ordinary firmware has been restored and
no production sound profile has changed. See
`../verification/output-measurement/README.md` for results, ordinary-firmware
restoration and the confirmed right-device startup magnetic calibration.

## Why measure

The user hears no obvious distortion in either the equal-average-level digital
pre-limiter or final-PCM files for the captured linked Harmony plan. On the
right device, the same plan was judged clean only after lowering its target
volume from .65 to .12222222222222229. No audio deadline, partial-write or output
error accompanied the current reproduction. This supports investigating the
physical output level; it does not identify a failed amplifier or speaker.

Offline extra 180/240/300Hz high-pass candidates were tested with the unchanged
C PCM processor and no gain compensation. At .65, the 300Hz candidate reduces
120–300Hz output by only 2.56dB and total RMS by .79dB, with the peak still .60.
Its low/mid output remains 7.74/8.95dB above the user-selected clean point.
There is no basis to advertise this as a sufficient distortion fix.

## Approved bounded trial

Use a diagnostic build on the right device, `4553502d49524953010030eda0f40c6c`.
Pause normal musical
playback and explicitly start a finite test; startup must never start it.
Use the board's ES8311 microphone to observe its speaker while playing known
PCM tones. The pinned BSP includes a microphone/speaker example, establishing
the available public codec interfaces; do not flash that example or copy its
continuous loopback behavior.

Test tones: 220, 330, 440, 660 and 1000Hz, with PCM peaks .12, .24 and .36,
short fades, approximately 350ms per tone and 200ms quiet spacing. Include a
short noise-floor/input-headroom check. Hard-stop each capture within 12 seconds,
with at most two explicitly initiated runs under this authorization, including
failed or uncertain starts across reboots. A local host ledger reserves each
attempt before submitting it and never automatically retries a start.
Do not raise the current hardware volume or exceed those tone levels. Input
gain is fixed within a run; ADC clipping invalidates the measurement rather
than being attributed to speaker distortion.

The existing audio owner must serialize TX/RX and all codec actions. Preserve
the phrase and explicit playback permission; completion or cancellation leaves
music paused. A visible indication and Pause cancellation must accompany the
capture. No audio input may be fed back to output. No recording at boot, no
continuous background capture and no persistence of microphone permission.

Keep samples in bounded PSRAM, then export only through the official Mosaico
workflow to local task artifacts. Prefer compact per-tone level/harmonic/noise
measurements and short waveform excerpts over hundreds of slow RPC chunk reads.
Do not upload recordings to a remote service or include unrelated speech in
reports. Ask the user to keep the measurement interval quiet; microphone
sampling can still include nearby sounds, which is why separate consent is
required beyond Echoy's normal touch-timestamp recording.

The result can compare relative distortion versus frequency/output level. It
is not calibrated SPL, an oscilloscope measurement, or proof of which analog
component is responsible. The microphone path's own noise/nonlinearity must be
considered. A measured or listening-supported voicing/output trial comes after
this observation; no production gain/volume cap is chosen in this proposal.

## Implementation and verification gates after consent

- Implement the explicit finite diagnostic state, TX/RX ownership, pause/cancel,
  input headroom checks and local readout. Keep it disabled in ordinary builds.
- Test no automatic capture/playback, hard duration and run limits, cancellation,
  stale generations, all partial I/O/start/cleanup failures and no automatic
  resumption. Never silently retry failed capture or hide a partial read.
- Build/inspect with the pinned SDK and retained Recovery/layout. Give concrete
  placement/start steps before collecting any microphone samples.
- Check the actual input for clipping/noise and report uncertainty. Restore the
  normal Echoy package after the bounded measurement and retain local evidence.

Firmware-install authorization already exists for the two verified devices.
The additional consent covers only this temporary microphone capture and its
finite test sound. It is not general recording permission. Give placement and
start instructions before each actual run; restore ordinary firmware afterward.
