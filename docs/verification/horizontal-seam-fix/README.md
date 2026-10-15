# Saturated horizontal left-contact correction

The user confirmed the failed B—C—A scene (B=f452a0, C=f40c6c,
A=f45936). An official planned restart of A reopened bounded readback while
joined, boot 2940862482323986586. All 55 valid MAG_LIVE records report left
X/Y=-32768, filtered Z=620..623, edges/stable/reported=0 and no left peer.
The pinned predicate rejects simultaneous X/Y saturation when Z<650.
That directly suppresses this confirmed horizontal seam before radio admission.

`echoy_horizontal_profile.h` supplies one common const Flash profile to every
board. Only the name and left_reject_z (650→600) differ from pinned S31 V1.
The pinned false-left TOP-shifted capture remains rejected at Z=560; the current
seam has at least 20 units above the new boundary. This is evidence for the
current three boards, not certification of all hardware or all magnetic fields.
No per-ID table, learned baseline, NVS calibration, radio-proximity substitute,
filter/debounce change, timeout change, SDK/BSP edit or music change is made.

## Verification

- `sh tests/horizontal_contact/run.sh`: original code fails the recorded seam;
  correction passes it, all 25 pinned BSP capture horizontal projections,
  599/600 boundary, four startup masks, filter/debounce, vertical exclusion and
  failed-read release. Captured fixture provenance is in the test source.
- Magnetic diagnostics (both configurations), physical adapter and related
  ASan/UBSan/LSan checks pass. See retained logs.
- Three-owner regression passes all six permutations/both join orders, large
  boot differences, bounded timeout and actual PCM; native chain adapter passes.
- Existing actual-owner ABC→BCA probe passes; it substitutes physical/radio
  input and does not establish a real magnetic seam result.
- Official SDK build command: `python3 output/three-device-20261015/build_firmware.py`.
  Run 20261015-163656-system-update-bundle-2 succeeds, with the known retained
  factory-size warning (the package targets OTA). Exact package is in
  normal-candidate.json; final ELF places the 44-byte profile in `.flash.rodata`.
- Accepted sound/UI source and packaged UI/layout match the accepted baseline.
  Office voicing stays enabled; magnet/audio/output diagnostics are disabled.
  Native application sources are unchanged from fixed-horizontal's fresh normal
  seven-group simulator result and prior reduced-motion evidence. Those are
  historical results, not a newly rerun simulator session for this threshold.

## Delivery status

Candidate f9b87dd4d4d7 prepared for all three under standing update authorization.
Physical installation and joined/rearrangement acceptance are pending. Keep the
user-confirmed B—C—A scene; joined startup no longer captures a baseline.
