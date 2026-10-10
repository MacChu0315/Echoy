# Role loudness trial (2026-10-10)

After confirming improved responsiveness, the user reported physical loudness
ordered Dust > Pulse/Voice > Harmony. Global hardware volume 90, PCM gain 6 and
the .90 peak ceiling remain fixed. The frozen HTML and raw shared synthesis are
unchanged by this trial; the performance optimizations remain in the firmware.

## Evidence and uncertainty

144 cases cover seeds 0x1134/0x1048/0x1085, all four roles and three phrases,
densities .5/1 and UI volume .65/1. The real C FULL loop is mixed with the shared
.45 bus and UI volume, then passed through the device PCM converter. One loop
warms the limiter; four loops (10 seconds) are measured. No sound was played
on the host. The installed FFmpeg `ebur128=peak=true` scanner supplies weighted
loudness and true-peak data ([official documentation](https://ffmpeg.org/ffmpeg-filters.html#ebur128)).
These digital measurements do not measure the board's acoustic response.

| UI volume | Pulse median LUFS | Voice | Dust | Harmony |
|---|---:|---:|---:|---:|
| .65 | -21.70 | -22.45 | -20.50 | -22.60 |
| 1.0 | -20.20 | -18.75 | -17.60 | -18.80 |

Dust's excess has measurable support. Voice and Harmony differ by only 0–.2 LU
in these digital measurements, so a large Harmony deficit is not established
as a software amplitude bug. Dust has about 86% of signal energy in 1–3 kHz;
Harmony about 76% in 300–1000 Hz and 24% in 100–300 Hz. Frequency content and
envelopes are relevant to the user's observation, but the speaker has not been
measured. [Findings](findings.json), [baseline levels](baseline.csv).

## Initial trial: Dust -2 dB, Harmony +1.5 dB

Pulse and Voice retain unity calibration; Dust receives -2 dB (.794328), and
Harmony +1.5 dB (1.188502). Harmony compensation is explicitly a user-listening
trial, not an inference of a measured digital defect.

The complete trial corpus has no sample clipping, peak <= .899990844 and maximum
reported true peak -0.4 dBFS. Dust moves by a median -2 LU at UI .65 and -1.4 LU
at UI 1.0; Harmony moves +1.5 LU. The four-role digital spread improves at .65
(median 2.1 to 1.5 LU) but worsens at full volume (2.3 to 2.9 LU), because Harmony
then measures above Voice. **Do not describe this profile as universally balanced
or acoustically accepted without the user's feedback.** If the Harmony boost is
not helpful, it can return to unity while retaining Dust attenuation. Only Dust
attenuation predicts smaller digital spread at both tested volume settings.
[Trial comparison](comparison.json), [trial levels](trial.csv).

## Implementation and checks

`echoy_band_calibrate_job` is an optional, once-only stage for an immutable
prepared job. It scales musical FULL buffers using the job's actual role;
changing the UI selection during preparation cannot apply the wrong gain.
PC/default callers do not invoke it. Existing crossfades retain their own bank
calibration, and tap/count-in/reference cues bypass role gains. No extra work
is added to the realtime render loop. Invalid gains reject the operation before
mutating buffers; a device calibration failure discards the prepared bank and
stops audio rather than silently playing an uncalibrated result.

- `sh tests/band/run.sh`: existing scheduling/link/detach behavior plus all-role
  gain, audible unchanged tap, invalid-input and duplicate-application checks
  pass. The initial no-op calibration failed the gain check as expected.
- The same suite passes ASan/UBSan/LSan.
- `python3 tests/device_runtime/test_runtime.py`: 22 cases pass, also with
  `ECHOY_RUNTIME_SANITIZE=1`. Includes failed calibration withholding playback.
- All 144 offline cases use the current limiter without sample clipping;
  Pulse/Voice WAVs and original shared synth buffers remain byte-identical.
- Official `system-update-bundle` build passes with no new compiler warnings.
  Device installation evidence and listening feedback are recorded below.

## Initial installation

Package `e45af2f7817a` installed successfully with operation
`92bcfd5b-75da-4a06-87e8-10ef52c73960`, HEALTHY boot `10838034974012344641`,
matching ELF identity and commit record, and unchanged bootloader/partition
hashes. The actual home screenshot is normal. Startup logs report musical role
gains 1000/1000/794/1188 milli and global PCM gain 6000 milli. No startup audio
error was observed. [Device evidence](device-result.json), [manifest](artifacts.json).
The user reported Dust still too loud. Logs and screenshot verified the new
Dust bank (seed `2898`, phrase 1, 13 events) had committed and played on this
boot with UI volume .65; there were no audio output errors or deadline misses.
No separate Harmony listening acceptance was supplied.

## Follow-up: Dust -4 dB

In response to that verified listening result, only Dust is lowered a further
2 dB, to .6309573445 (-4 dB total); other roles and global output remain as in
the initial trial. 144 cases now use the exact production ordering: a float
bank gain multiplication, then double UI volume and bus mix, then the unchanged
PCM limiter. Dust falls another 2.0 LU at UI .65, and 1.4–2.0 LU (median 1.65)
at UI 1.0. Other roles' LUFS do not change. No sample clipping; maximum sample
peak .899990844447, maximum reported true peak -0.6 dBFS.

Digitally, Dust is now below Voice by a median 1.85/2.0 LU. This is intentional
board-listening calibration based on the user's feedback, not a universally
flat numerical profile. [Comparison](dust4-comparison.json), [levels](dust4-levels.csv).
Band/calibration checks and 22 runtime cases pass with the new setting; the
calibration regression also passes ASan/UBSan/LSan at the new setting.

Package `4db21a252792` then installed successfully with operation
`b23665b0-9768-4b90-bcea-76c836b15f78`, HEALTHY boot `6559752351850501042`,
matching image/commit and unchanged bootloader/partition hashes. The actual home
screen is normal. Boot logs confirm gains 1000/1000/630/1188 milli, global gain
6000 milli, and no idle output errors or deadline misses. [Current install](dust4-device-result.json).
The user subsequently rejected -4 dB too: Dust was still very noticeably loud.

## Direct listening match

The same -4 dB firmware/boot was verified, including actual Voice and Dust bank
changes for seed `1128`, with no output errors or limiter activity. The user
kept Voice at the existing nominal .65 volume, then reduced Dust's slider until
they reported the sounds were close. The screenshot moved the knob center from
y=177 to y=224. From the UI mapping (`249 - round(volume*111)`), the final position
represents about .225 volume. This is quantized visual readback, not an exact
numeric volume RPC or an acoustic SPL measurement.

The corresponding bank gain is `.630957 * .225 / .65 ~= .219`. The selected
coefficient is `.22` (about -13.15 dB total), within the slider's readback
resolution. At the usual .65 slider position it reproduces the user's matched
Dust level. Pulse/Voice unity, Harmony 1.188502, cues, global gain6, hardware90
and the limiter are unchanged. No spectral filtering or synthesis change is
introduced. [Listening evidence](listening-match.json),
[Voice reference](match-reference-voice.png), [adjusted Dust](match-adjusted-dust.png).

`sh tests/band/run.sh` and `python3 tests/device_runtime/test_runtime.py` pass
(22 runtime cases). Another 144 cases use the actual compiled role-gain table:
zero clipping, Dust peak <= .361674856, all 108 other-role WAVs byte-identical to
the -4 dB corpus. [Checks](matched-checks.json), [levels](matched-levels.csv).
The pinned official firmware/bundle build passes. This calibration is based on
the user's board listening comparison; it is not a claim that the unweighted or
EBU digital levels of all roles are equal.

Package `52b40e645630` installed successfully with operation
`d6ee981f-6813-43ef-9056-9fdd856b60c3`, HEALTHY boot `572815562769689448`,
matching image and commit record, and unchanged bootloader/partition hashes.
The home screenshot is normal. Actual startup logs confirm role gains
1000/1000/220/1188 milli, global gain 6000 milli and no output errors or deadline
misses. [Installation](matched-device-result.json), [artifact](matched-artifacts.json).
The user matched the pre-installation slider levels; a separate post-installation
listening verdict has not yet been received.
