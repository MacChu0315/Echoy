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

The 16 cases cover:

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

The event-group fixture deliberately delays the pinned SDK's post-unlock read
in `xEventGroupSetBits()`. Successful cases assert that the group is not deleted
until both producer calls have returned. The task fixture also detects querying
a terminated task's stack watermark. The cleanup-failure case intentionally
retains the runtime handle, matching its uncertain-hardware-ownership contract.

These are lifecycle/controller tests, not an emulation of FreeRTOS priority
scheduling, actual I2S DMA, PSRAM bandwidth, embedded stack limits, DAC timing or
physical audio. Those require the separately approved target-device acceptance.
