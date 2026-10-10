# Device loudness limiter trial

The user rejected the gain-3 device's loudness even with the UI slider at its
maximum. On 2026-10-10 they approved trying louder output with peak limiting and
its effect on musical dynamics, and authorized subsequent Echoy upgrade-package
installations on the same verified Mosaico without per-package confirmation.
This does not authorize remote repository writes or imply acoustic acceptance.

## Implementation and limits

Only the device PCM stage changes: gain 6 instead of 3 (+6.02 dB before limiting),
ceiling .90, 24-frame / 1 ms lookahead at 24 kHz, and an 80 ms release time
constant. Hardware volume remains 90. The frozen HTML, shared synth, musical
rules, UI, PC output and reference diagnostic tone remain unchanged.

A sliding peak window holds attenuation over the delayed sample. A 24-sample
moving average smooths the gain transition; exponential release restores gain
without a hard step. Each averaged gain includes that delayed sample in its
peak history. A final numerical bound enforces the ceiling despite roundoff.
No additional runtime allocation or synthesis-worker dependency is introduced.

This intentionally changes musical dynamics. It does not promise transparent
sound, eliminate analog distortion, or compensate for speaker frequency
response. The small added output delay is fixed; cancellation, audio faults,
reopening and diagnostic transitions clear cached samples and gain reduction.

## Verification

- `sh tests/audio/run_device_pcm.sh`: quiet signals receive the intended gain;
  bounded peaks, 24-frame latency, arbitrary-block continuity, release recovery,
  reset silence and whole-block invalid-input rejection pass. The first gain
  regression failed against the previous gain-3 implementation as expected.
- 144 actual synthesized plans (four seeds, four roles, three phrases,
  densities 0/.5/1): RMS .075803821–.121500099; improvements over gain 3 range
  from 3.251600 to 6.020607 dB. 55 plans engage attenuation. All 2,304 mixtures
  (ten audition taps/second, 24 offsets, including prior-bar tails) pass through
  the real stateful converter with no clipping; peak <= .899990844 after PCM
  quantization. [Levels](levels.csv), [checks](checks.txt).
- `python3 tests/device_runtime/test_runtime.py`: 21 cases pass.
- `ECHOY_RUNTIME_SANITIZE=1 python3 tests/device_runtime/test_runtime.py`: all
  21 cases pass with ASan/UBSan/default LeakSanitizer. The pause-tail regression
  also fails when the production reset-on-flush call is removed in an isolated
  temporary copy, confirming it detects leftover audio at the board boundary.
- Pinned SDK `system-update-bundle` build and official bundle inspection pass.
  Artifact `4ee7991a5d56` contains application plus unchanged UI/partition table;
  no bootloader or retained Recovery component. [Manifest](artifacts.json).

The first runtime compilation caught a missing diagnostic format argument; it
was corrected before passing tests and before the package build. No unresolved
local test failure remains. The waveform checks alone do not establish acoustic quality.

## Physical installation

Operation `a312e9a0-1c45-46f6-a9d8-b6cb8c60602f` installed the package on the
verified board after Recovery initialization. The official updater matched
ELF `8ef8ce037e9ffcf57e77ac0e202dac37b1dbf57ae272f6caf69b806a31707d73`,
HEALTHY on boot `15517228967953464410`, the commit record, unchanged bootloader
and partition hashes. The actual screen reached home; startup logs confirm gain
6000 milli, no clipping, no output errors and no estimated deadline misses.
[Device evidence](device-result.json). User touches recorded seed `a404`, played
its one-shot reply, and repeatedly started/paused playback. At 93,583 ms uptime,
telemetry showed 154,196 nonzero frames, peak PCM 29,490 (the intended .90 ceiling),
116,086 samples under gain reduction, and zero clipping, partial writes, output
errors or estimated deadline misses. Maximum combined render/conversion time
was 3,211 us per 10 ms block; owner stack minimum was 8,744 bytes. PA was off at
capture because the user had paused; three activations were retained. Phrase
preparation still took 10,654,753 us and remains a separate unresolved issue.
Listening feedback is pending; no additional reference tone was requested.
