# Device speaker voicing trial (2026-10-10)

The user found the .60-ceiling package substantially less distorted but all four
roles too quiet. A gain-only scan showed why a global increase was insufficient:
at .65 UI volume, gain6 -> gain12 raised median Pulse/Harmony RMS by only
1.38/2.03 dB, versus Voice/Dust 4.53/6.02 dB. The user then explicitly approved
attenuating roughly sub-120 Hz content and adjusting compression/role balance,
prioritizing louder, less-distorted device sound over the original bass weight.
The same-board continuing installation authorization remains applicable.

## Implemented profile

- Device PCM only: second-order 120 Hz Butterworth high-pass at 24 kHz.
  Single-precision coefficients are computed offline; no per-sample allocation,
  trigonometry or software-double filter processing is introduced.
- PCM drive gain18, peak ceiling .60, unchanged 1 ms lookahead and 40 ms release
  instead of 80 ms. This is stronger compression, not transparent amplification.
- Musical bank trims Pulse/Voice/Dust/Harmony = 1.6/.52/.0317/2.38, selected to
  raise default-volume mean levels together rather than over-boosting Dust.
- The common PCM stage also processes audition/count-in cues; those cues bypass
  musical-bank trims but receive the new filter/drive/limiting. Their scheduling
  is unchanged. The opt-in reference diagnostic path continues to bypass PCM.
- Filter state lives with the existing PCM owner, spans block boundaries and is
  cleared with limiter state on pause/fault/reopen. Preflight rejects nonfinite
  and extreme finite input before changing output or state, retaining arithmetic
  headroom for filter feedforward/feedback sums.
- No change to frozen HTML, shared synthesis, note pitches, rhythms, UI or
  neighbor scheduling. This trial does intentionally change device timbre and
  dynamics; PC playback does not receive the adaptation.

## Verification

- `sh tests/audio/run_device_pcm.sh` passes: measured quiet-sine response near
  40/120/1000 Hz is within independent Butterworth bounds, DC settles to zero,
  latency/peak/reset/block continuity/release and invalid-block atomicity pass.
  The filter-response check failed against the previous flat response.
- The same suite passes the actual `40aa` Harmony headroom regression and a
  four-role `8234` comparison against retained .60-ceiling/gain6 RMS fixtures.
  All four roles increase by 2.5–5 dB at .65 volume while staying below .60002.
  The new mean-level regression fails against commit `6881123` as expected.
- All 144 synthesized plans and 2,304 tap mixtures remain within the .60 ceiling
  with no clipping; existing gain-over-3x checks still pass. These broad cases
  do not substitute for the calibrated profile comparisons below.
- PCM tests additionally pass ASan/UBSan/float-cast-overflow checks. Runtime
  tests pass 22/22 normally and with ASan/UBSan/LSan outside the ptrace sandbox.
  `sh tests/band/run.sh` passes scheduler and bank-calibration tests.
- 288 calibrated cases cover six seeds including `1128`, `40aa` and `8234`, all
  four roles and three phrases, densities .5/1 and UI volumes .65/1. Zero sample
  clipping; peak <= .599993897 and FFmpeg true peak <= -3.9 dBFS. All 120 matched
  raw synthesis banks are byte-identical to the previous trial.
  [Checks](checks.json), [levels](levels.csv).
- Versus the installed .60-ceiling/gain6 profile, median default-volume RMS
  increases are Pulse +3.40, Voice +3.17, Dust +3.02, Harmony +3.31 dB. At full
  volume they are +3.63/+2.20/+3.02/+2.36 dB. These are digital measurements,
  not acoustic SPL or a guarantee of perceived equal loudness.
- Pinned SDK/official `system-update-bundle` build passes with zero warnings,
  application size `0x17e760`, 15% partition headroom. Local raw build log:
  `.codex-runs/idf-low-noise-build/20261010-164212-system-update-bundle-2/raw.log`.
  The build uses `output/device-audio-ui-build`; the runner's default artifact
  summary points at the older `projects/echoy/build`, so the actual bundle and
  generated binary were verified independently before installation.

## Physical installation and remaining acceptance

Package `7c431dacfd59` installed on the verified Mosaico
`4553502d49524953010030eda0f452a0` using the official workflow and ready Recovery.
Operation `2c3e8697-aaac-425b-a11a-0f9043b396a9` succeeded with HEALTHY boot
`6467976503804649253`, matching image identity/commit and unchanged bootloader,
partition table and UI resources. [Artifact](artifacts.json),
[device evidence](device-result.json).

The actual home appears normally. Startup logs confirm musical gain milli
1600/520/31/2380 and PCM gain milli 18000, with no audio write/deadline errors.
Playback seed `2154` is observed, with Pulse preparation 346,311 us, peak PCM
19,660, render/conversion maximum 3,281 us and owner stack minimum 8,744 bytes.
Clipping, partial-write, write-error and estimated deadline-miss counters remain
zero. These are Pulse measurements, not all-role timing acceptance.
The user reported hearing no obvious distortion, while wishing it could be
louder. This is positive listening feedback, not measured THD or complete
loudness acceptance. A further same-ceiling gain scan completed without another device write: at
full UI volume, gain18 -> gain48 adds only .75 dB median Harmony RMS, versus
1.83/3.49/8.52 dB for Pulse/Voice/Dust. This does not support a useful balanced
gain-only upgrade. [Further scan](further-gain-scan.json). The clean-listening
profile remains installed. Meaningful further acoustic gain needs measured
speaker/output-chain evidence or a separately evaluated spectral adjustment;
these calculations do not establish an absolute hardware loudness limit.
The scan covers seeds `1128`/`40aa`/`8234`, all roles/phrases, density .5,
volumes .65/1 and gains 18/24/30/36/48 (360 cases).
More average power at the same
sample peak can still expose analog/speaker distortion; no measured acoustic
frequency response or waveform is available. Do not call the entire loudness
problem resolved on the strength of the numerical checks.

## Original expression listening check

The positive listening report is supported by playback of all four roles, but
the current package's Harmony plan was phrase 2 (Broken), not the originally
reported phrase 3 (Answer). The user then selected Answer and explicitly confirmed that it also had no
obvious distortion at the same higher volume. This closes the expression-specific
listening follow-up for this trial; it is user observation, not a measured THD
or SPL claim, and does not establish every possible rhythm or speaker limit.

The user subsequently decided to retain the current volume ("音量上就先这样吧")
on 2026-10-10. Keep package `7c431dacfd59` and this profile; stop further loudness
tuning unless requested. This provisionally closes the tuning task without
claiming a measured acoustic limit.
