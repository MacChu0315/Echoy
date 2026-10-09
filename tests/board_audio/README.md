# Board speaker adapter verification

The adapter uses the pinned BSP at `a2c985ad698b3f04a21c63e006b43e7e948bc1d7`,
managed `esp_codec_dev` 1.6.2 and IDF at `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`.
It borrows the BSP revision-aware I2C bus/power rails and owns its TX-only I2S0,
codec control/data interfaces, ES8311 interface and codec device. It never calls
the BSP's cached audio factories, deletes the shared bus, or switches off shared
power during cleanup. GPIO40 microphone input remains unassigned.

The driver accepts 24 kHz/16-bit mono. Its mono format mapping selects the left
slot in the two-slot I2S bus; ES8311 has a matching 6.144 MHz MCLK coefficient.
No software stereo duplication or resampling is necessary. The adapter checks
the actual public I2S mode configuration after codec opening. DMA is 3 by 120
mono frames (720 bytes, 15 ms capacity), not a claimed measured output latency.

`open` leaves PA GPIO45 and DAC muted. Hardware volume stays at 60 (the codec's
default volume curve gives -20 dB before its board-gain compensation). The first
accepted nonzero block unmutes; band/UI digital gain is unchanged. On a write
fault PA is disabled before any I2C work, then TX stops. Cancel/flush disables
PA, mutes, stops TX, zeroes every DMA descriptor with the public preload API and
restarts muted. The scheduler must separately discard its stale mixed PCM.

The pinned codec facade ignores some lower-layer errors. ES8311 also acquires
an enable reference before doing I2C, but on failed startup cannot release it
through `enable(false)`. The public control proxy records the first I2C error,
skips subsequent register traffic in that operation, and lets the driver's
bookkeeping complete. The adapter checks that recorded error before ever
enabling PA or reporting success, then uses ordinary public cleanup calls.
This behavior is covered by a regression stub reproducing the SDK's reference
counting and ignored returns. No private SDK object layout is used.
The default volume curve is also installed through the checked public setter:
the pinned constructor otherwise silently tolerates allocation failure and
would interpret nonzero volume as 0 dB without a curve.

Cleanup consumes the application handle. If an SDK resource destructor itself
fails, later opens are rejected until reboot; the adapter cannot reconstruct a
freed SDK interface's private resource handle. This prevents repeated Recover
requests from accumulating resources. Display/touch/ESP-Iris Recovery stay
independent; the adapter never aborts, resets the MCU or switches their power off.

Run the host checks (no real hardware access):

```sh
sh tests/board_audio/run.sh
BOARD_AUDIO_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie' sh tests/board_audio/run.sh
```

These tests compile against the real managed codec public headers and stub only
the hardware/BSP/codec calls. They check format, mono bytes, bounded DMA waits,
partial writes, mute ordering, queue clearing, error propagation, reference
cleanup, retry and failed-destructor lockout. The stubs model known codec bugs;
accepted nonzero PCM, peak magnitude (including -32768), write duration and PA
transitions are also checked. These diagnostic counters are not measured sound.
They do not prove electrical behavior, acoustic level, board timing or real
underruns. Those require a separately authorized device run. LeakSanitizer needs
execution outside a ptrace-restricted sandbox; do not treat its sandbox failure
as a successful leak check.
