# Finite output measurement signal checks

Run `sh tests/output_measurement/run.sh` or append `--sanitize` for
ASan/UBSan/LSan. Leak checking requires a host environment without the ptrace
restriction. The tests use independently synthesized libm inputs and do not
open audio hardware.

The first physical trial on 2026-10-14 used the original 9-second sequence:
216000 frames at 24 kHz, a 0.5-second lead, and a noise window from 0.1 to
0.4 seconds. Its local results and consent entry remain unchanged.

The following diagnostic revision uses 252000 frames (10.5 seconds). It extends
the quiet lead to 2 seconds and measures noise from 1.2 to 1.8 seconds, allowing
more settling before the tones. First-tone analysis now begins at frame 49800,
instead of 13800. This adjustment does not establish the cause of the first
trial's unexpected level measurements.

Both profiles retain the same 15 tones, PCM peaks .12/.24/.36, 350 ms tone
duration, 200 ms gaps, 20 ms fades, and final 250 ms silence. The 12-second hard
limit and total consent budget of two attempts are unchanged. Production
musical playback is unaffected.

Coverage includes exact duration, arbitrary render boundaries and end padding,
all frequencies and levels, harmonic ratios, phase and +/-50 ms delay, DC,
low signal/noise, ADC clipping anywhere in the capture, incomplete input and
cancellation. The revised noise-window test injects separate large offsets
before 1.2 s and after 1.8 s to catch accidental use of startup samples.

`python3 tests/measurement_rpc/test_host.py` also validates both historical and
current profile metadata. Collection rejects mixed frame counts, noise-window
lengths or tone-window starts, and never rewrites earlier evidence or resets
the fixed consent ledger.
