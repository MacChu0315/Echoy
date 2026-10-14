# Board speaker adapter verification

The adapter uses the pinned BSP at `a2c985ad698b3f04a21c63e006b43e7e948bc1d7`,
managed `esp_codec_dev` 1.6.2 and IDF at `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`.
It borrows the BSP revision-aware I2C bus/power rails and owns its TX-only I2S0,
codec control/data interfaces, ES8311 interface and codec device. It never calls
the BSP's cached audio factories, deletes the shared bus, or switches off shared
power during cleanup. Normal `board_audio_open` leaves GPIO40 microphone input
unassigned, including when the diagnostic option is compiled in.

The driver accepts 24 kHz/16-bit mono. Its mono format mapping selects the left
slot in the two-slot I2S bus; ES8311 has a matching 6.144 MHz MCLK coefficient.
No software stereo duplication or resampling is necessary. The adapter checks
the actual public I2S mode configuration after codec opening. DMA is 3 by 120
mono frames (720 bytes, 15 ms capacity), not a claimed measured output latency.

`open` leaves PA GPIO45 and DAC muted. Hardware volume stays at 90 (the codec's
default volume curve gives -5 dB before its board-gain compensation). This was
raised after the physical device remained very quiet at 60: captured PCM peaks
were about 13% of full scale before the additional hardware attenuation. The first
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


The separately gated `board_audio_measure_open(out, input_gain_db,
hard_deadline_us)` requires `CONFIG_ECHOY_OUTPUT_MEASUREMENT=y`, a closed normal
handle, and an absolute `esp_timer_get_time()` deadline no more than 12 seconds
away. It opens an application-owned I2S TX/RX pair at the same 24 kHz, signed
16-bit mono LEFT format and uses ES8311 ADC/DAC mode. It explicitly selects
0/6/12/18/24/30 dB microphone gain before enabling the codec; the pinned facade
restores that same setting on open. Hardware output volume remains 90. After
clearing all DMA descriptors to silence, diagnostic open unmutes the DAC and
enables PA so the caller's initial quiet interval can settle the output chain
before noise analysis or the first tone. Statistics record one PA activation
and zero accepted PCM frames at this point; the later first nonzero block does
not repeat unmute/PA activation. Normal TX-only open remains muted until its
first accepted nonzero PCM. No call manufactures tone data, reads microphone
samples automatically, or forwards captured audio to TX. Disabled
builds return `ESP_ERR_NOT_SUPPORTED` without allocating diagnostic hardware.

The diagnostic proxy uses public I2S enable/disable calls separately, starting
TX before RX and stopping RX before TX. This is intentional: the pinned codec
data adapter's `IN_OUT` enable and format paths overwrite the TX result with
the RX result; single-direction disable can also be deferred. The pair has an
immutable application-specified format, checked on both actual public channel
configurations after codec open. The normal TX-only format verification and
its original proxy path remain strict and unchanged.

`board_audio_measure_read` reports every complete consumed sample even on
partial failure, rejects short/odd/oversized transfer results, and never retries
a returned prefix. A DMA receive-queue overflow latches an atomic flag in an
IRAM callback with static internal-RAM storage. The callback performs no
hardware operation; the owner detects the flag before/after PCM I/O and fails
the measurement instead of treating silently dropped samples as a continuous
waveform. Setup queue contents are discarded when the diagnostic DMA pair is
cleared and restarted, before returning the explicit handle.

PCM reads/writes use at most one descriptor per transfer and share a 40 ms
operation deadline, further capped by the absolute capture deadline. Timeouts
are rounded down for diagnosis, including the final fractional millisecond.
The public I2C adapter has a 100 ms transaction timeout (legacy reads have two
transactions); the proxy refuses to start a control transaction without that
much time remaining. A timeout or streaming failure stops PA and RX/TX before
codec work. Close likewise stops PA/RX before codec cleanup; **any diagnostic
cleanup error** blocks subsequent normal and diagnostic opens until reboot.
Flushing a diagnostic returns an error and stops it; cancellation must close.

The finite runtime must continuously service this handle and close it on every
exit. There is no second timer task or independent hardware watchdog. The IDF
channel-disable implementation uses internal `portMAX_DELAY` semaphores; the
single-owner rule removes normal contention, but host tests cannot establish a
bound under an OS/driver failure. I2C cleanup runs after RX/PA shutdown and may
outlast the capture deadline. Failed shutdown is reported, never claimed to
prove that an unresponsive physical codec accepted its power-down writes.

`run.sh` runs both option states, then separate diagnostic I2C-close,
RX-disable and resource-destructor failures to verify permanent reopen lockout.
Coverage includes ordinary TX-only operation, both paired formats and DMA
sizes, invalid gains/deadlines, allocation/setup failures, gain before capture,
no automatic reads/nonzero PCM, silent diagnostic PA settling, exact successive
sample prefixes, partial/odd/
oversized reads, DMA queue overflow, per-I/O and absolute deadlines, late I2C
refusal, cancellation, cleanup ordering and resource/reference release. The
settling failure cases cover unmute I2C failure, insufficient deadline reserve
before unmute, expiration or RX overflow during unmute, PA GPIO failure, and
expiration during PA activation; each leaves no application handle, PA output,
RX/TX channel, or codec reference alive. The
sanitizer invocation above keeps AddressSanitizer, UndefinedBehaviorSanitizer
and LeakSanitizer enabled. These are host boundary checks, not an acoustic or
physical microphone test.
