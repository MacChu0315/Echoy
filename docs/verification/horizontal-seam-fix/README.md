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

Package `f9b87dd4d4d7` is installed/HEALTHY on all three under standing
authorization; A is back on the normal build, with diagnostics disabled.
Per-device operation, startup, screenshots and retained component results are
stored alongside this record. No startup baseline capture is performed.

The user reports C and A both show following in B—C—A. The read-only
`user-bca-following` snapshots confirm B cooperation=1, C/A following=true and
cooperation=4, valid committed banks, no pending preparation, all paused and no
application crashes. This captures the eventual current arrangement, not a
clean joined-start acceptance: intervening physical release/contact events and
an A boot change are visible. Earlier snapshots showed invalid same-edge
sessions and C solo. B/C also logged transient ESP_ERR_ESPNOW_NO_MEM while
sending; the later follow state converged, but sustained radio health is open.

A probe linked against the unmodified actual BSP session/topology sources
reproduces four committed same-edge (180-degree) slots in this BCA identity
order, instead of four complementary slots (exit1). The generic BSP supports
those rotations; Echoy accepts only same-orientation horizontal edges and
rejects them after allocation. This is an additional startup/negotiation gap,
not fixed by the magnetic profile. The probe is diagnostic only; no production
session behavior has been changed. Source and output are preserved here.

The user requests restarting acceptance from startup and confirms all three
separated. Official planned restarts and independent-start checks pass on all three:
no contact events, no baseline capture, empty Record-only screens, SOLO owners,
zero output errors and crashes (startup-checks.json). These are software restarts,
not physical power-cycle tests. Independent A recording, A—B silent following with C separate, and A—B—C
silent following all pass user checks and read-only owner verification. All
three keep the same software-restart Boots. The original ABC→BCA transition
is requested next; no result yet. The early test05 capture was taken before
physical movement and still shows ABC; it must not count as a BCA result. Do not declare
all physical three-device behavior accepted from the current BCA report.
