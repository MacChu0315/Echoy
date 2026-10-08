# Audible three-tile verification

This supersedes the earlier UI-only completion boundary. The desktop now runs
actual shared C synthesis, linked-plan generation and PCM scheduling behind
three official GSP 1.5.1 hosts. The frozen HTML remains unchanged.

## Verified results

- `node tests/arrange/oracle.mjs`: **864 linked plans and 1552 model transitions**
  match the unchanged `ArrangeLinked` / `ArrangeModel` JavaScript. This includes
  pitch/harmony inheritance, support placement, invalid/cyclic chains, stale
  jobs, edge-only commits, exact detach retention and independent loop clocks.
  ASan/UBSan/LeakSanitizer also pass outside the ptrace sandbox.
- `sh tests/audio/run.sh`: **225028 behavior and 60 error/ownership checks** pass.
  The same tests and reference comparison pass ASan/UBSan.
- `node tests/audio/oracle.mjs /tmp/echoy-audio-oracle` and
  `python3 tests/audio/compare.py /tmp/echoy-audio-oracle`: **99/99** actual
  Chrome 151.0.7922.75 `OfflineAudioContext` references pass. Maximum relative
  waveform error is **1.1858e-5**, worst SNR **98.52 dB**, onset difference
  **0 samples**. [Per-sound measurements](audio-comparison.json).
- `sh tests/band/run.sh`: real nonzero PCM, shared starts, independent pause,
  detach retention, recovery silence, identical re-recording, stale automatic
  reply cancellation and shared language pass. ASan/UBSan pass.
- `cmake --build /tmp/echoy-band-model` and `ctest --test-dir
  /tmp/echoy-band-model --output-on-failure`: **1/1**, all eleven recording/model
  behavior groups pass, including managed preparation and externally supplied
  clocks.
- `python3 -m unittest discover -s tests/desk_preview -v`: **12/12** pass.
  This includes fixed commands, private socket/file access, same-origin HTTP,
  owned-process cleanup, capture-path forwarding and rejection of stale ready
  state when a control directory is reused. Browser fixture checks separately
  pass real dragging, snapping, detach, shelf/keyboard controls, 220ms boot and
  450ms connection settling. Official iframe canvas bounds remain 480x480 at 1:1.
- `sh tests/desk_runtime/run.sh`: failed WAV writes are reported; an owned stopped
  audio child is terminated and reaped within the bounded shutdown path.
- `python3 tools/check_home.py --build-dir projects/echoy/build-sim-desk
  --output output/audio-home-regression`: all **9** original input/recording UI
  checks pass. This is explicitly the standalone UI-only regression adapter.
- `python3 tools/check_desk.py --build-dir projects/echoy/build-sim-desk-final
  --output /tmp/echoy-desk-native-global-language`: **11/11** full integration
  checks pass with three actual GSP hosts and real pointer input. The test
  snapshots the executable/bundle, waits for visible controls before clicking,
  and records PCM instead of accepting a changing animation as sound evidence.
  [Checks](checks.json), [tested hashes](runtime-snapshot.json),
  [actual session export](session.json).

The native run covers ordinary tap sound without recording, one-shot reply,
A→B→C inheritance without auto-start, individual Play on a common bar, independent
pause, B role/phrase/density changes reaching C at the boundary, exact detach,
fault/recovery, remove/restore, export, and language changing all three screens.
Its 60.01-second WAV has peak **6970/32767**, no clipped samples, and **9185
consecutive paused frames exactly zero**. Three hosts and three UI owners exit
normally with no GSP errors; `late_blocks=0`. [PCM measurements](audio-metrics.json).

Screens: [A](A.png), [B](B.png), [C](C.png),
[English A](english-A.png), [English B](english-B.png), [English C](english-C.png).

## Actual computer output

A separate non-silent 22-second run used the installed PulseAudio output, without
changing system volume. PulseAudio exposed the uncorked `Echoy A+B+C` stream as
**s16le, mono, 24000Hz**. The native example produced **22475 nonzero samples**,
peak **6197**, and then stopped; `samples_written=528000`, `late_blocks=0`, clean
exit. [Stream metadata](pulse-stream.json), [capture metrics](speaker-output.json),
[short excerpt from that adapter capture](native-example.wav).

The full 11-check run uses the silent output adapter to avoid repeatedly playing
its long test sequence; it still records the actual shared mixer PCM. The
non-silent run separately establishes connection and sample delivery to the
computer audio service. Neither result proves acoustic output timing at a
physical Mosaico speaker.

## Build, ownership and limits

The fixed `idf_low_noise_build.py --project projects/echoy build` succeeds for
ESP32-S31 with **zero warnings**; run `20261008-121014-build-2`, firmware
**1,369,392 bytes**. This compiles the portable modules; the physical app entry
still uses its UI-only adapter, so this is not a hardware sound acceptance build.

A prepared SOURCE/FOREGROUND/FULL bank occupies approximately **720KB**; preparation
uses another **323KB** temporarily. The worker owns immutable jobs outside the
band lock. Stale jobs release their buffers; active playback keeps referenced
banks until completion/fade. File status/export operations copy snapshots under
the lock and write outside it. GSP operations stay on each handle's owner thread.

The reference checks establish 24kHz offline synthesis/normalization. They do not
claim bit-identical live taps across arbitrary browser sample rates or already
running compressor states, certified perceived loudness, physical CPU/heap
watermarks, wireless synchronization or acoustic hardware timing. The physical
speaker/output adapter, neighbor discovery, communication and real peripherals
remain to be integrated. No device was flashed and no remote was written.

The current desktop service responds normally, but the computer-use browser
client rejected automatic navigation to its loopback URL with a client-side
block. Browser security settings were not changed. The browser fixture and
native protocol tests above remain the actual verified scopes.
