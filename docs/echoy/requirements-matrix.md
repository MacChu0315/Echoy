# Echoy requirement-to-evidence matrix

Recorded 2026-09-12. The scope is the fixed HTML and
[approved design](../superpowers/specs/2026-09-11-echoy-mosaico-design.md).
`PC pass / HW pending` means the portable implementation was exercised, but
the physical requirement is **not accepted**. `Build pass` is not device health.
Evidence paths are relative to the Echoy root; `output/task10/` and `output/final-fix/` contain local,
ignored raw evidence. No simulated device is presented as a physical board.

| Requirement | Implementation | Automated verification | Physical/manual acceptance still required | Evidence | Status |
| --- | --- | --- | --- | --- | --- |
| Fixed HTML identity and complete source extraction | `reference/manifest.json`, `tools/extract_reference.mjs` | Exact source/module hashes; oracle self-test | None for source identity | `output/task10/extract.log`, `oracle.log` | PC pass |
| Four roles × three phrases and role defaults | `echoy_core/phrase.c`, `harmony.c`, `core.c` under `components/` | 48,600 oracle cases / 216,510 events; native units | Play all 12 on the actual speaker | `output/task10/js-c.log`, `ctest-final.log` | PC pass / HW pending |
| Activity 0/default/1 and threshold neighbors | `phrase.c`, `response.c`, `harmony.c` | Every documented 0.001 threshold neighbor, defaults and endpoints | Actual expression controls and low-density audibility | `output/task10/js-c.log` | PC pass / HW pending |
| 96 BPM, 16 steps, D minor and deterministic numeric rules | `echoy_core/`, `pc/core_runner.c` | Oracle field comparison, malformed input regression, generated voicings | Record real tempo/drift over time | `js-c.log`, `music-green.log`, `voicings.log` under `output/task10/` | PC pass / HW pending |
| Repeat roles, full left motif, Harmony voicing/variants | `response.c`, `harmony.c` | 48 native three-device chains / 360 boundary cases | Duplicate physical roles and all Harmony variants in both chain directions | `output/task10/js-c.log` | PC pass / HW pending |
| Current versus pending role/phrase/activity | `echoy_core/core.c`, `session.c` | `state_unit`, `state_oracle`, `platform_control`; current/pending UI assertions | Change settings during actual output and inspect the boundary | `output/task10/ctest-final.log` | PC pass / HW pending |
| Immediate live expression, bar-boundary rhythm density | `core.c`, `echoy_audio/audio.c` | State/oracle and DSP tests | Actual expression/timbre and boundary latency | `output/task10/ctest-final.log` | PC pass / HW pending |
| Local volume 0/.70/1, independent of expression | `echoy_audio/audio.c`, `echoy_ui/touch.c` | DSP smoothing/output, volume hit grid and modal tests | Actual silence and gain at each setting | `ctest-final.log`, `visual.log` under `output/task10/` | PC pass / HW pending |
| Play on next beat; immediate pause; waiting cancellation | `core.c`, `echoy_platform/control.c` | State oracle, `platform_control`, headless play/reboot scenarios | Actual button response and audible stop | `output/task10/headless.log` | PC pass / HW pending |
| Role preparation preserves old music; failure/cancel clears candidates | `core.c`, `control.c`, `view.c` | State units, injected PC readiness/failure, all status screenshots | Actual preparation/error paths where reachable | `headless.log`, `visual.log` under `output/task10/` | PC pass / HW pending |
| Paused/output-failed/volume-zero tile still supplies motif | `core.c`, `protocol.c`, `audio.c` | State and three-owner fault tests | Two-tile response while the source is silent or output has failed | `output/task10/ctest-final.log` | PC pass / HW pending |
| Data failure/recovery changes source only at a boundary | `core.c`, `protocol.c`, `control.c` | Two real wire owners, coordinator/follower fault/loading, pre/post apply revision/motif/source and recovery | Two-tile fault/recovery with physical output | `output/final-fix/control-final.log` | PC pass / HW pending |
| Entire 480×480 perform/role/status/volume UI | `echoy_ui/view.c`, `touch.c` | 74 frozen LVGL/reference comparisons; label clipping checks | Actual screen direction, brightness, readability and orientation | `output/ui/metrics.json`, `output/task10/visual.log` | PC pass / HW pending |
| Four animated figures and 16-step event dots | `view.c`, runtime/PC live snapshot updates | Captured arc/region guards; actual Pulse/Voice event frames, decay, stale clock, waiting/resume/coalesced silent-window exclusion and C live onset pixels | On-device smoothness and measured sound/display latency | `output/ui/metrics.json`, `output/final-fix/round2/target.log`, `ctest.log` | PC pass / HW pending |
| Full Chinese/English and generated font corpus | `i18n.c`, `assets/fonts`, `generate_ui_fonts.py` | 231 glyphs, 24 font sizes; 74 screens; both actual browser LVGL title pixel crops | Read small physical glyphs and inspect clipping on board | `output/task10/fonts.log`, `browser.log` | PC pass / HW pending |
| 84px controls, drag direction, modal isolation/cancel | `touch.c`, `view.c` | Full 480×480 hit grids, native pointer regressions, real Chrome mouse/touch/keyboard | Physical sensor alignment and ≤100ms touch confirmation | `output/task10/visual.log`, `browser.log` | PC pass / HW pending |
| Real local DSP kick/rim/hat/tone/grain/chord and tails | `echoy_audio/audio.c`, `output.c`, `speaker.c` | 22 C/Chromium WAV cases; onsets, tails, density, voice and clamp tests | Actual ES8311 writes plus speaker recording/listening | `output/task10/audio-metrics.json`, `audio/`, `ctest-final.log` | PC pass / HW pending |
| Sample scheduling, stale-clock cancellation and bounded output | `audio.c`, `output.c`, `runtime.c` | DSP missed-time/clock/capacity regressions; pending COMMIT forecast tests | Measure DMA presentation and forecast margin under mirror/UI/NVS load | `output/task10/ctest-final.log` | PC pass / HW pending |
| Discovery alone does not admit/synchronize/start | `echoy_core/protocol.c`, `echoy_platform/control.c` | Separate boot/discovery and three-owner admission tests | Separate physical devices, then establish an edge | `output/task10/headless.log`, `ctest-final.log` | PC pass / HW pending |
| Correct physical UID, directed edges, simultaneous-edge ambiguity | `contact.c`, `radio.c`, `protocol.c` | Actual BSP classifier with fake sensor samples; UID/direction/topology rejection | Two/three boards, sequential admission, first sandwich ambiguity | `output/task10/ctest-final.log` | PC pass / HW pending |
| A→B→C/C→B→A, third tile joins without auto-play | `control.c`, `protocol.c`, `core.c` | Three real C owner instances and queued packet scenarios | Three identified physical boards and direct left response | `output/task10/headless.log`, `js-c.log` | PC pass / HW pending |
| Separation removes response while retaining shared beat | `session.c`, `protocol.c`, `control.c` | State oracle, release/keep-membership tests | Real magnetic release and continuing radio/audio | `output/task10/headless.log` | PC pass / HW pending |
| Stable measured clock, fresh readiness, continuous fallback | `clock.c`, `protocol.c`, `session.c` | ±rate clocks, unstable/stale sample rejection, 12-hour simulated drift | Actual RTT/residual/rate/loss distribution | `output/task10/ctest-final.log` | PC pass / HW pending |
| Reference loss, re-election, old boot/session/transaction rejection | `wire.c`, `protocol.c`, `control.c` | Delay/drop/reorder/duplicate/partition/reboot and final COMMIT loss | Physical reference power-off/reboot and old-packet conditions | `output/task10/ctest-final.log` | PC pass / HW pending |
| Settings persistence and paused boot | `storage.c`, `runtime.c` | CRC/schema/range/finite validation; debounce/error and reboot scenarios | Reboot board after settings, confirm paused and retained identity | `output/task10/ctest-final.log`, `headless.log` | PC pass / HW pending |
| Battery <15 warning, errors retain valid charge, controlled 0 shutdown | `battery.c`, `runtime.c` | Battery/storage units and PC injected low/zero charge | Controlled real battery and GPIO57 shutdown; preserve sysmeta | `output/task10/ctest-final.log` | PC pass / HW pending |
| IMU/module diagnostics only; no musical side effects | `runtime.c`, `diagnostics.c` | Component inspection plus diagnostics/storage tests | Real cached module/IMU observations | `docs/echoy/hardware-acceptance.md` | Code/PC pass / HW pending |
| Bounded callbacks, immutable snapshots, mirror lifetime | `radio.c`, `runtime.c`, `screen_mirror.c`, `speaker.c` | Queue saturation, allocation/error paths, sequential/random-access mirror tests; ASan/UBSan | Continuous Iris mirror/audio contention and memory return | `output/task10/ctest-final.log`, `sanitize-ctest.log` | PC pass / HW pending |
| Lost development touch release cannot trap the next gesture | `touch_delivery.c`, `runtime.c`, `view.c` | Actual LVGL/control: busy-lock UP/CANCEL, repeated contention, one cleanup, next Play waiting, no extra activity/volume; diagnostics counts | Physical sensor delivery follows BSP/LVGL directly; hardware release-loss testing remains pending | `output/task10/round1/touch-red.log`, `touch-green.log`, `ctest.log` | PC pass / HW pending |
| Retained Recovery, correct partitions and explicit app health | `main/main.c`, `.mosaico.json`, `sdkconfig*`, `partitions.csv` | Built table decoded, image size/config checked; normal and dev IDF builds | Same Device ID normal→Recovery→normal, changed Boot IDs, ready and healthy | `output/task10/firmware-contract.json`, `idf-clean-build/`, `device-recover.stderr` | Build pass / HW blocked |
| Normal firmware has no development injection | `main/Kconfig.projbuild`, `runtime.c` | Default-off generated config; only method 1 registered in normal branch; injection validation rejects disabled calls | Probe normal board and restore normal after any dev install | `output/task10/firmware-contract.json`, `touch-rpc-green.log` | Build/PC pass / HW pending |
| Complete external desktop/shelf/guide/localization/fault panel | `tools/echoy-sim/run.py`, `firmware.js`, unchanged HTML shell modules | 29 headless scenarios, 23 Chrome checks, 13 JS adapter/guide checks | Desktop reviewed separately from device UI | `output/final-fix/headless.log`, `browser.log`, `wall-green.log` | PC pass |
| Browser disconnect cannot stop physical autonomy | `firmware.js`, independent `runtime.c` | Hide/pagehide/network failure invalidates unsent PC requests; explicit reconnect | Play board with the computer disconnected | `output/task10/browser-adapters.log` | PC pass / HW pending |
| 30-minute continuous output plus touch/mirror without leaks or underruns | `runtime.c`, `speaker.c`, mirror/UI components | Wall-clock PC endurance is additional evidence only | Full single-board run plus three-board endurance; measure task/heap/UI/touch/DMA | `output/task10/host-endurance/` | 30min PC pass / HW pending |
| Acoustic P50/P95/max and drift, P95≤10ms/max≤20ms | Local DSP/clock mapping; no acoustic measurement implementation | No host simulation can accept this requirement | Calibrated common multichannel recorder, fixed placement and propagation correction | No physical recording available | Unverified |
| Real speaker low frequencies, grain texture, tails and noise | Local DSP→BSP→ES8311 path | Numeric spectrum/envelope comparison only | Human listening and actual speaker recordings | `output/task10/audio-metrics.json` explicitly says listening not performed | Unverified |
| Last peer loss restores local parameter commits | `session.c`, `control.c` | Playing/paused hold expiry, continuous epoch/rate, role/phrase/activity commits and protocol rejoin | Physical last-peer loss and rejoin | `output/final-fix/control-final.log` | PC pass / HW pending |
| Protocol failure is visible without replacing old music | `control.c`, `core.c`, `view.c` | Dropped COMMIT: protocol/core failure agrees; old revision/events retained; successful renegotiation clears feedback | Physical loss/divergence feedback during audio | `output/final-fix/control-final.log` | PC pass / HW pending |
| Interactive PC clock and bounded PCM follow elapsed wall time | `firmware.js`, `pcm-worklet.js`, `platform_pc.c` | Shipped App.start with 60/126 ms responses; full 250 ms PCM, bounded queue, worklet continuity, disconnect/reconnect | No hardware claim; preview prefill adds latency | `output/final-fix/wall-green.log`, `pcm-green.log`, `browser.log` | PC pass |

## Final integration review

The control task remains the sole core/protocol/settings writer. Radio and UI
callbacks copy bounded commands. Audio owns its DSP/codec; publications copy
the full plan and a monotonic audio-clock generation invalidates forecasts.
Screenshots take their own immutable PSRAM surface and do not take an audio
lock. Host tests cover random-access reads, continuous pull refresh and
allocation failure/cleanup. None of this measures real PSRAM contention.

The normal-to-Recovery partition/inventory contract was inspected in the actual
installer, not inferred from its filename. Final device-write evidence is
recorded in the hardware acceptance document. The scaffold's file-existence
unit is still shallow, so `firmware-contract.json` separately records the
actual built partition table, normal Kconfig and slot capacity checks.

Deferred review items were evaluated against acceptance confidence:

- Fixed: numeric oracle booleans and malformed primitives in unknown C-runner
  JSON fields, with failing-before/passing-after regressions.
- Fixed: browser contact acceptance now waits for the actual published edge;
  the fixed 300ms delay failed under concurrent build load. Locale now checks
  real native title pixels in both languages. Six guide observation conditions
  are independently asserted rather than inferred from Next-button clicks.
- Hardened: Chrome profile removal uses bounded filesystem retries; audio
  reference generation waits for its explicitly loaded local document before
  installing the synth after an observed `DuijuSynth`-missing startup failure.
- Deferred: remaining dynamic i18n string placement is an organization issue;
  both rendered languages are covered. Curve antialiasing/speaker arc polish
  needs actual performance evidence before further drawing work.
- Deferred: member UID/boot arrays, pending edge endpoints and finer protocol
  rejection categories would improve multi-board diagnosis. Existing local
  UID/boot mapping, peer clocks and aggregate errors remain explicit; no
  multi-board failure diagnosis is claimed from those aggregates.
- Deferred: extractor read-only mode policy and the reference CDP client's
  close/error cancellation require separate harness cleanup; source hashes
  and the completed 74-frame reference check protect this acceptance run.

No project dependency or sibling source was changed to hide a failure.
The installed Recovery tool did create its own ignored build artifacts in its
owned directory; Echoy source development remains entirely in this project.
