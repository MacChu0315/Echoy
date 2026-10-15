# Echoy development rules

- Reusable, task-scoped Mosaico techniques are indexed in `.agents/skills/README.md`.
  Select only relevant skills; these do not replace the rules or device authorization below.
- The approved design is `reference/v4/echoy-arrange-B-v4.html`; hashes are in
  `reference/v4/SHA256SUMS`. The v3 files and original v4 ZIP remain historical baselines. Never edit this baseline to make an implementation pass.
- Preserve its 480x480 product layout, wording, icons, typography, pages and
  interactions. Keep browser desk/debug controls outside the device screen.
  Show concrete differences and obtain approval before a non-equivalent change.
- Approved device adaptation: hide "Put away this tile (simulation)" on hardware;
  retain the action in the PC simulator and leave the frozen HTML unchanged.
- Approved Display behavior: manual brightness only (20–100%, initially 100%).
  Idle time, touches and playback must not change the selected brightness.
- Use ESP-GSP JSON/resources and portable C UI shared by the native `sim_bridge`
  and firmware. Do not substitute a whole-screen image for functioning controls.
- Validate UI and interaction in the matching official simulator before hardware.
  Scene-only renders do not validate C callbacks; `--frames`, `--fps` and
  `--dump-ppm` select scene-only mode in the current Vibe wrapper.
- Keep application-owned files under this repository. Firmware lives in
  `projects/echoy/`; `pc/` beneath it excludes board, FreeRTOS and Iris dependencies.
  Consume public utils/BSP interfaces; do not copy their CLI or modify siblings.
- Dependency revisions and tool versions are recorded in `docs/dependencies.json`.
  Use GSP 1.5.1 / GSPC 0.6.1 and the pinned ESP32-S31 SDK, not arbitrary latest tools.
- Recording captures touch timestamps, not microphone audio. Preserve initial
  silence, three one-second preparation counts, four recording beats,
  cancel/empty-record behavior, one-shot reply and explicit per-device continuous-play permission.
- Preserve the retained Vibe Mode/Recovery partition and integration contract.
  Device writes use the official `mosaico.py` workflow and current user authorization.
  Never infer physical audio, wireless or timing acceptance from PC results.
- The user authorizes subsequent Echoy upgrade-package installations on the
  verified Mosaicos `4553502d49524953010030eda0f452a0` (initially `/dev/ttyACM0`)
  and `4553502d49524953010030eda0f40c6c` (initially `/dev/ttyACM1`), including
  dual-device test versions, without per-package confirmation. Verify each
  identity/artifact, retain Recovery and use the official workflow; USB port
  names may change. This is not authorization for remote repository writes.
- Product capacity target: at most three Mosaicos in the same scene, restricted
  to same-orientation horizontal chains. The v3 physical adapter and independent
  group owners are implemented locally; device delivery/physical acceptance is
  tracked in the project state. Preserve the accepted dual-device rollback.
  V2/v3 mixtures must not establish cooperative following; use one verified v3
  package for all members. First Play remains prompt during a locked update;
  keep its boundary fixed and align unchanged banks there as well.
- Physical linking uses the pinned BSP's magnetic classifier and ESP-NOW network.
  Start radio/RF initialization before audio activation; network and sensor I/O
  stay off the audio owner. Keep boards separated on a nonmetal surface during
  startup magnetic calibration, then wait for the baseline-ready logs before
  testing a horizontal seam. Never substitute radio proximity for physical contact.
  A paused/non-following or "not connected" cached state does not prove a
  calibration-safe magnetic field; a stable field can reject startup even with
  zero classified edges. Reconfirm physical placement before a later restart
  if the user has handled the devices since the last placement check.
- Approved cooperation: physical release ends membership/following and the old
  clock group immediately while retaining the current phrase and playing/paused
  permission. Radio-only recovery keeps physical membership. A paused follower
  uses centered Play; all-paused first Play may come from either side promptly,
  and clock exchange never grants a paused peer playback permission.
- Approved audio trial: device-only loudness gain with peak limiting may change
  musical dynamics. Preserve the frozen HTML and musical rules. Numerical
  synthesis optimizations must pass the existing HTML audio comparison budgets;
  physical listening acceptance is still required.
- The user accepted the office speaker profile with softened Harmony on
  2026-10-14. Subsequent device builds must retain
  `CONFIG_ECHOY_OFFICE_VOICING_TRIAL=y` (the flag keeps its historical name;
  defaults are in `projects/echoy/sdkconfig.office-voicing-trial.defaults`).
  Preserve this sound unless the user requests more tuning. The frozen HTML
  and ordinary reference renderer remain unchanged; device rollout is in the
  project state file.
- Keep `docs/PROJECT_STATE.md` current with verified progress and remaining work.
  User-supplied verification claims are provenance, not new test results.
- Sound and linkage acceptance must run the shared C synthesis/arrangement and
  actual PCM scheduler. A UI-only backend, scene render or separately recreated
  browser sound model is insufficient. Keep audio timing independent of GSP RPC
  and file I/O; preserve immutable preparation jobs and cancellation generations.
- The PCM owner must not wait for UI/link cached-state readers. Publish without
  blocking and retry on a later block; keep the output deadline and explicit
  pause/recovery semantics intact.
- Retained GSP encoded images and the large UI view cache use the device
  `ui_storage_alloc` PSRAM path. Hiding a bind does not release its image source.
  A UI with both external control callbacks must not create a redundant
  standalone musical model.
- Home artwork is prepared by a private worker from copied frame/plan data;
  GSP remains the sole touch reader and UI owner. The worker must not call GSP,
  flash or filesystem APIs. Keep its large storage/stack in explicit PSRAM;
  borrowed Canvas buffers remain immutable until release. Stop/flush failures
  must retain live contexts for retry.
