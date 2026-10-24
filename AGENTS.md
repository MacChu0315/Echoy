# Echoy development rules

- Follow the pinned dependencies in `docs/dependencies.json` and commands in
  `docs/development.md`. Use GSP 1.5.1/GSPC 0.6.1 and the pinned ESP32-S31 SDK.
- Keep application-owned files in this repository; consume public utils/BSP
  interfaces without changing sibling dependencies. `projects/echoy/pc/` must
  remain free of board, FreeRTOS and Iris dependencies.
- Preserve the frozen v4 reference and hashes under `reference/v4/`. Implement
  the shared portable C UI with ESP-GSP resources; do not replace live controls
  with whole-screen images. Edit generators rather than generated resources.
- Preserve the 480x480 layout and approved interactions. Hardware hides the
  simulator-only put-away action. Brightness is manual 20–100%; idle, touch and
  playback must not change it.
- Set initial panel brightness before GSP starts; do not race main-task panel
  commands with rendering. Keep display scratch in PSRAM and I2S interrupts
  IRAM-safe during the host's post-HEALTHY Flash inventory reads.
- Bound the LCD's internal DMA transfer queue separately from render scratch.
  Iris screenshots show the rendered shadow, not proof of physical LCD output;
  verify transfer logs and actual screen behavior across warm and cold starts.
- Echoy has no haptics: explicitly disable the motor at app entry, including
  early-failure paths. A shutdown error must preserve Iris access and stop
  further application startup.
- Validate UI behavior with actual C callbacks in the matching official native
  simulator. Scene-only rendering and UI-only audio models are insufficient.
- Recording captures touch timing, not microphone audio. Preserve initial rests,
  three-second count-in, four-beat capture, cancel/empty behavior and one-shot
  reply. Continuous playback requires explicit per-device permission.
- Keep local audition randomness and input guards independent of recorded seeds,
  musical banks, group transactions and peer playback permissions.
- Preserve the office speaker profile, including softened Harmony:
  `CONFIG_ECHOY_OFFICE_VOICING_TRIAL=y`. Numerical optimizations must meet existing
  waveform comparison budgets; physical listening remains a separate check.
- Support at most three same-orientation tiles in a horizontal chain. Use shared
  read-only Flash `echoy_horizontal_profile`; do not restore per-boot learning.
  Keep filtering, sampling readiness and complete unique-neighbor admission.
- Use `mosaico_peer_link` and application-owned radio admission. Never infer
  contact from radio proximity or release from radio silence. Retain first-wire
  timestamps across retries; wait for TX completion before the next raw send.
- Physical release ends membership/clock while preserving phrase and playback
  permission. Radio-only recovery retains completed state but aborts pending
  transactions. Failed SDK teardown must not free or reuse live contexts.
- Initialize RF before audio. Keep sensor/network I/O, synthesis, allocation,
  GSP RPC and filesystem work off the PCM owner. Preserve immutable preparation
  jobs, cancellation generations and nonblocking cached-state publication.
- Keep the core-1 PCM owner above GSP and the pinned Iris/TinyUSB service
  priorities. Equal-priority time slicing must not delay ready DMA buffers;
  preserve the existing audio deadlines and buffer sizes.
- Prepare home artwork on its private worker from copied frame/plan data. GSP
  owns touch and UI operations. Keep large UI/art storage in explicit PSRAM;
  borrowed buffers remain immutable until release. Failed stop/flush retains
  live contexts for retry. Avoid duplicate models with external controllers.
- Device updates require task authorization, verified identities/artifacts and
  the official `mosaico.py` retained-Recovery workflow. Never infer hardware
  timing, wireless or acoustic acceptance from host tests.
- Run checks appropriate to the changed subsystem. Keep public status and release
  notes concise. Do not commit private device snapshots, local paths, user
  conversations, unpublished experiment logs or firmware binaries. Existing
  historical verification fixtures required by tests must remain available.
- Keep `docs/PROJECT_STATE.md` as a public overview. Keep private working notes
  outside tracked release content. Frozen archive branches must not receive
  ongoing status-only updates.
