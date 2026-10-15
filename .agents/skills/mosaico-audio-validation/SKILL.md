---
name: mosaico-audio-validation
description: Use when validating Mosaico synthesis or PCM changes, or investigating speaker loudness, timbre, clipping and audible distortion. Not for microphone recording features, generic media editing or UI-only checks.
---

# Mosaico audio validation

Separate reference correctness, digital output and physical listening. Determine
the requested behavior and already accepted sound before changing gain or timbre.
Use the pinned board/codec configuration; derive parameters for this product
rather than importing another application's calibration.

## Preserve the exact experiment

Capture available plan bytes/hash, seed, expression, density, actual UI volume,
profile/configuration, firmware and Boot ID. A later cached plan may not be the
plan heard during the fault. Label a reconstruction as reconstructed, and keep
it separate from captured playback. Compare candidates on identical inputs.

| Layer | Evidence and its limits |
| --- | --- |
| Reference algorithm | Actual shared C versus approved reference; events, envelopes, spectra and boundary inputs |
| Digital output | Actual PCM conversion/scheduler; peaks, RMS, limiting, partial I/O, pause and faults |
| Physical output | Speaker/listener observations or controlled acoustic measurements at known placement and levels |

Do not substitute a separate browser synthesizer for the C implementation.
Retain existing comparison budgets when preserving behavior. Check extreme
pitches, buffer ownership, stale jobs and output equivalence across chunk sizes.
Changes to accepted dynamics or timbre need authorization at that scope.

## Diagnose before tuning

Locate the first stage where the unwanted result appears. Check source synthesis,
mixing, calibration, limiting, PCM conversion, codec gain and the physical path.
Digital headroom does not prove speaker headroom: codec or amplifier gain may
follow the limiter. Equal RMS is not equal perceived loudness across spectra.
Measure preparation/output timing so a dropout is not mislabelled as distortion.

Use bounded candidate comparisons, then controlled listening at the intended
distance and setting. Preserve accepted roles/profiles while changing the scoped
problem. Record a user's acceptance as listening provenance, not a measured SPL,
phase offset or diagnosis of a defective component.

## Keep diagnostics bounded

For microphone diagnostics, confirm authorized device, stimulus, capture scope,
duration, attempt budget and data retention before capture. Registration must
not start recording. Keep diagnostics off in ordinary builds; explicit start,
cancellation, partial I/O and deadline failures must have defined cleanup and
must not unexpectedly resume playback. Preserve an attempt ledger if limits
span restarts; uncertain submissions consume the reserved attempt.

Background speech can invalidate harmonic measurements even when quiet-window
SNR checks pass. Do not tune from contaminated data or silently rerun exhausted
attempts. Continue permitted offline analysis, report the gap and propose a new
controlled experiment if needed. Verify the ordinary configuration after restore.

Report numerical results, listening results and remaining physical uncertainties
separately. Never equate a clean waveform or successful update with clean sound.
