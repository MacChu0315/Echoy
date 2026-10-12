# Consistent initial expression selection

The user requested consistency after confirming Harmony alone defaulted to its
middle expression. All four roles now initially select the first/left card:
Pulse Accent, Voice Short phrase, Dust Grains and Harmony Short chords. Selecting
another expression and switching away/back retains the user's selection for the
running session. No persistence or stored-data migration was added.

The standalone model, shared arrangement model (including initial Harmony tile B)
and hardware loading cache now agree. Musical generation, speaker voicing and
the recently accepted three-second timings are unchanged. UI resources and the
frozen HTML are unchanged.

## Verification

- New standalone/shared-band tests failed against the prior defaults before the
  production edit. They now pass first visits to all roles and preservation of
  each later manual selection after switching away/back.
- `cmake --build build-host` and `ctest --test-dir build-host --output-on-failure`:
  2/2 pass. `sh tests/band/run.sh` passes real shared-controller, PCM cue,
  cancellation, role memory and calibrated-bank checks.
- `sh tests/arrange/run.sh`: 864 linked plans and 1,552 state transitions pass both
  normally and with ASan/UBSan. The test creates the unchanged frozen JavaScript
  model, then adapts only its initial expression/memory defaults to the explicitly
  approved first-card rule before any seed/plan exists. All musical algorithms
  and subsequent state transitions remain the original oracle; the frozen file's
  SHA-256 check remains unchanged.
- `python3 tests/device_runtime/test_runtime.py`: 22/22 pass, including an added
  assertion that the initial hardware loading cache uses expression 1 for every role.
- Official GSP 1.5.1 / GSPC 0.6.1 backend/desk builds pass.
  `python3 tools/check_full_ui.py --build-dir projects/echoy/build-sim-default-expressions
  --duration-ms 90000 --output output/default-expressions/ui` passes. Every role's
  first visit is expression 1; revisits retain expression 3 selected earlier.
  All twelve expressions render and commit real plans in Chinese and English;
  settings, brightness, finite audition and quiet recovery also pass.
  [Results](ui-checks.json). All four initial-gallery screenshots were visually
  inspected and show the left card selected.
- Pinned SDK `system-update-bundle`: exit 0, warnings 0, run
  `20261012-110049-system-update-bundle-2`. Actual app size: 1,567,888 bytes.
  Package `1d57c5719989` uses the retained layout; its UI resource hash is identical
  to the previously installed `9ef37e48c8b0`. [Artifacts](artifacts.json) use the
  actual build directory, not the low-noise runner's stale default artifact path.

## Physical installation (2026-10-12)

Official `mosaico.py iris system-update --bundle ... --device-id ... --endpoint ...`
installed package `1d57c5719989` on the same verified Mosaico, operation
`d6e52c8b-6412-424c-9d9b-f51e848ba5e2`. HEALTHY boot `5127593241566643399`
has the expected ELF identity, commit and unchanged bootloader/partition table.
[Device evidence](device-result.json), [actual home](installed-home.png).

`iris device-status`, `iris screenshot` and `iris logs --snapshot` confirm normal
mode on the same boot after 100 seconds, application crash count zero and no
UI/audio write/deadline errors. Startup logs retain the same bank trims and PCM
gain. Native GSP verified all four gallery selections; no physical pointer
injection or automated gallery switching was performed on the device.

Before package writing, the old retained Recovery 0.1.2 again reset during startup:
boot `6268870116642677245`, reset reason 5 (`ESP_RST_INT_WDT`), failed address
`0x20000`, unchanged Recovery image `54f72372…`. The helper waited for Recovery
boot `9896472446416298202` to finish initialization and saved its crash report;
no valid Core Dump existed. It then submitted one successful update. This
pre-existing Recovery startup problem is retained as an unresolved maintenance
issue, not attributed to the new application defaults.
