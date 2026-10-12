# Active mixer regression

Run `sh tests/render_active/run.sh`. The fixture retains the exact pre-optimization
`echoy_band_render` from commit `5fa0d8f` as a test-only oracle. Two independent real
bands receive identical actions and preparation results, then their PCM buffers
are compared bit for bit; volume-smoothing state must also match exactly.

The 96 comparisons cover all twelve role/expression pairs, silent volume changes,
future/expired recording and tap cues, commits occurring inside one render call,
preview starts, continuous loops, new-bank crossfades, cancellation and cues from
multiple voices. A cache of active slots must refresh after an in-block commit;
otherwise the preview/replacement tests expose missing sound. Slot order remains
significant because reassociating floating-point additions can change output.

This is a behavior-preservation test, not a host speed threshold. The original
performance failure was measured on the ESP32-S31: the first silent 240-frame
block exceeded its physical output deadline, and cycle profiling attributed
most mixing time to repeated empty cue-slot scans. The actual before/after
hardware measurements belong in `docs/verification/dual-device/`.

ASan/UBSan/LeakSanitizer:

```sh
RENDER_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' sh tests/render_active/run.sh
```

LeakSanitizer needs execution outside a ptrace sandbox; do not reinterpret that
environmental restriction as a passing leak check.
