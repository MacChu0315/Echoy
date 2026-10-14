# Device runtime host checks

These tests compile the **production** `platform_device.c`, band, model, music,
arrangement and synthesis sources. The board audio/health adapters and the
FreeRTOS synchronization/task API are replaced with pthread-backed test fixtures.
Nothing opens a device, sends sound, accesses the network or modifies firmware.

```sh
python3 -m unittest discover -s tests/device_runtime -v
ECHOY_RUNTIME_SANITIZE=1 python3 -m unittest discover -s tests/device_runtime -v
```

The optional sanitizer build enables AddressSanitizer, UndefinedBehaviorSanitizer
and the normal leak checks. Some restricted execution environments prevent
LeakSanitizer from inspecting threads; this produces its `ptrace` fatal error,
not a passing leak result. Run the same local test outside that restriction.

The original 27 runtime checks remain. The suite now has 39 test methods,
including diagnostic failure subcases, and separately compiles an ordinary
build to verify that its measurement API cannot open a microphone. They cover:

- Cached UI reads while the audio write is blocked; unknown battery/IMU values.
- Valid loading-state plan metadata and phrase memories before band creation.
- Record followed by Cancel in one FIFO batch, with no delayed capture or cue.
- Queue overflow closing output, discarding stale actions and requiring recovery.
- Partial PCM acceptance, initialization failure, failed flush and late output.
- Recovery preserving the seed without restoring playback permission.
- Empty battery remaining latched when the next health read is unknown.
- Shutdown during startup or immutable preparation, timeout retention and retry.
- Partial task creation, health cleanup retry and audio cleanup error propagation.
- Audio remains unopened during UI setup/firmware acceptance, activates once on
  explicit release, and shuts down safely if acceptance fails before activation.
- Measurement requires explicit admission, one of the six fixed input gains,
  PSRAM storage and at most two accepted starts per boot, including runtime
  recreation. Completion preserves the phrase and clears continuous playback.
- Capture writes and reads exactly 252000 frames, then closes RX before the
  existing low-priority preparation worker analyzes and erases raw PCM. A
  blocked worker cannot block silent output, and shutdown waits for ownership.
- Any submitted UI action cancels the run. Queued cancellation, overflow, partial reads or
  writes (including a falsely successful partial return), input/output faults,
  hard-deadline expiry, initialization, flush, close and normal-reopen failures
  all stop capture without automatically resuming music.
- A slow measurement-cache reader cannot delay capture or cancellation. Old
  analysis and a cancellation arriving immediately before result publication
  cannot publish a stale result. Failed shutdown can retain lifecycle context
  for retry but still erases raw microphone samples after both workers exit.

The microphone fixture renders the known sequence into the capture buffer and
paces each 240-frame diagnostic block at 1 ms to keep these host checks short.
It verifies exact sample bounds and explicitly injects deadline expiry; it does
not establish 10.5-second wall-clock pacing or real DMA service margins. The
production owner skips link/model/preparation/view-copy work while RX is open.

The event-group fixture deliberately delays the pinned SDK's post-unlock read
in `xEventGroupSetBits()`. Successful cases assert that the group is not deleted
until both producer calls have returned. The task fixture also detects querying
a terminated task's stack watermark. The cleanup-failure case intentionally
retains the runtime handle, matching its uncertain-hardware-ownership contract.

These are lifecycle/controller tests, not an emulation of FreeRTOS priority
scheduling, actual I2S DMA, PSRAM bandwidth, embedded stack limits, DAC timing or
physical audio. Those require the separately approved target-device acceptance.
