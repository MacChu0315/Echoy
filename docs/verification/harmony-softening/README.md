# Harmony softening after office listening

The user accepts Pulse, Voice and Dust timbre/loudness from `7307f8ed9a3e`,
and reports no obvious Harmony breakup. Harmony is nevertheless especially
loud, sharp/bright and too electronic. This refinement changes only Harmony;
it does not reopen the other three roles or alter notes, timing, expression
rules, UI, common PCM processing, hardware volume or microphone authorization.

## Selected change

The office Harmony waveform now uses fundamental/second/third amplitudes
`.60/.20/.10`, plus the original triangle component. Added fourth/fifth
harmonics are removed. Second/third phases remain `0/3pi/2`. The Harmony trim
changes from `1.6` to `.8`; other office trims stay `1.6/.78/.0634`.
The accepted 500 Hz bass cut remains. The shared reference renderer and
the original Source/Foreground buffers, target and encoded plan are unchanged.

Two explicitly bounded candidates and three trims each were compared through
actual C/PCM. B/.8 retains more clarity than the still softer A/.8, while having
fewer/smaller increases in the 300–1000 Hz band. Higher tested trims would raise
more of that band and reduce the excessive overall level less.

The after-feedback captured plan was **Dust**, paused at UI 1.0, seed `95a8`
with seven source notes. Its three Harmony expressions are reconstructed with
`echoy_compose` from those actual source notes, not described as captured
Harmony playback. At the same full UI level, selected-versus-installed R128
changes are -4.7/-4.9/-3.8 LU for the three reconstructed expressions; the
1–3 kHz band falls about 13–14 dB. The 300–1000 Hz band changes by
-2.282/+.316/+.785 dB, so not every band falls in every case.

Across 72 grid cases, median R128 change is -5.3 LU, median 1–3 kHz change
-14.19 dB; nine cases raise 300–1000 Hz slightly, at most +.518 dB. These are
digital observations, not a universal clean-output threshold or proof of
pleasant physical sound. The historical actual Harmony `8534` phrase at
.761111 volume now has driven peak .572382 and PCM RMS .134548 with no effective
limiting. Its full-volume output can still reach the unchanged .60 ceiling.
See `comparison.json` for the selected results and their exact provenance.

## Verification

- `sh tests/speaker_voicing/run.sh --sanitize`: real DSP, atomic invalid/OOM
  behavior, unchanged reference banks, harmonic/note probes and high-note
  alias rejection pass with ASan/UBSan/LSan outside the ptrace sandbox.
  The new softness assertion first failed on the installed bright profile.
  The old >.23 RMS floor/<12 dB relative-crest target was explicitly superseded
  by this user's request for lower Harmony level: the new regression checks
  actual driven headroom below .60 and useful lower PCM energy instead.
  Removing the Nyquist guard fails the updated second-harmonic folding check.
- `sh tests/band/run_speaker_voicing.sh` with ASan/UBSan/LSan: all six ownership,
  failure/stale/common-bar/Pause groups pass using real DSP.
- `python3 -m unittest discover -s tests/device_runtime -k office -v`:
  seven applicable office/configuration tests pass. No new worker architecture
  or runtime behavior is introduced by these parameter changes.
- `sh tests/audio/run.sh`: 225028 behavior and 60 ownership/error checks,
  performance and extreme-pitch checks pass. The first command was accidentally
  invoked from `reference/v4` and did not run; the correct repository-root
  rerun above passed. Frozen v4 SHA checks pass.
- Bounded numerical comparison: 539 cases; all checked Source/Foreground,
  target, plans and PCM block partitions are preserved. Final actual-product
  comparison has zero FULL/PCM differences in 77 Harmony cases against B/.8
  and 218 other-role cases against `ad71b58`. This includes the actual captured
  Dust plan. See `selected.json` for source hashes and complete scope.
- `python3 tools/check_desk.py --build-dir output/build-sim-office-voicing
  --output output/harmony-refinement-20261014/native-check`: 13 official native
  GSP/shared-C profile/PCM groups pass, including exact paused silence and
  common changes continuing old sound. The existing silent verification adapter
  uses one combined desktop PCM chain, not separate physical speakers.
- The configured pinned-SDK `system-update-bundle` build passes; log:
  `output/harmony-refinement-20261014/build.log`. Package inspection retains
  Recovery/OTA offsets, partition hash and identical UI data. Both diagnostic
  audio-entry configurations remain off. The existing unused-factory-size
  warning is unchanged; this package targets OTA.

## Physical trial

Package `5d4776c9ae6d` contains ELF
`aacb24e1acc4e0a4f11a7cb10b08645f4d0a6e930e98ff19e9ae1675d79a01c7`.
The official update is left-only under the existing authorization; immediately
before it, the cached left state was still paused, not following or connected,
and unchanged from the captured Dust plan. The user had been asked to retain
the separated setup, but physical placement was not freshly confirmed. The
first installed boot `15367590955867785783` rejected a stable baseline with
right_y 917/919/918 and classified edges zero. This demonstrates that cached
unconnected state was insufficient to establish a calibration-safe field.

The user then placed the boards at least 15 cm apart away from magnetic/metal
sources and manually restarted left. Current boot `1456888021128116879` has
the correct ELF, zero crashes, actual home and silent startup, and logs baseline
ready at 2114 ms. Installation operation `4a18849d-b4ed-4e98-8ede-ba7071fcd78d`
is HEALTHY/validated with retained bootloader/partition hashes; no second write
occurred. `AGENTS.md` now records the physical-placement distinction.
Right remains ordinary `09275b935ad8`. Evidence is in `physical-install.json`.

The new softness, balance and continued absence of breakup require user
listening; do not turn the preceding version's no-breakup report into acceptance
of this changed Harmony. No microphone capture or computer playback occurred.
