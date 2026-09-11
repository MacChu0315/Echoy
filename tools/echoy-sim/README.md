# Echoy desktop acceptance tool

Run from the Echoy project root (Python standard library, CMake and the existing
host dependencies only):

```sh
python3 tools/echoy-sim/run.py --mode reference
python3 tools/echoy-sim/run.py --mode firmware
python3 tools/echoy-sim/run.py --headless --scenario all
```

Both web modes bind **127.0.0.1:8765**. Use `--port 8879` when that port is busy.
Reference mode serves the unmodified SHA-256-pinned HTML. Firmware mode is an
explicit **PC development simulator**, not a connection to hardware. It builds
and starts three `echoy_sim_runner` processes linked to the same `echoy_core`,
`echoy_control`, `echoy_ui` and `echoy_audio` as the firmware. The initial desktop
uses Pulse/Voice with Dust in the reserve, matching the reference demonstration.

Only the original shell, localization, spatial adapter and observing guide are
included in the firmware webpage. The original JS product engine, session
model, product renderer and synthesizer are not loaded. Every `.screen` is an
actual 480×480 LVGL framebuffer; screen pointer coordinates go to
`echoy_view_pointer`. Shell coordinates stay in `DuijuSpatial` and only directed
contact identities cross the bridge. The six-step guide, restart, locate,
quick-reference text, shell drag/snap/collision/cancel, keyboard shell movement,
desk scroll, reserve/power, language, fault panel, log and mapping remain outside
the product screens. A mouse click on the shell handle starts a drag; use Tab to
focus a handle before using arrow keys, as in the reference.

AudioWorklet plays bounded 48 kHz stereo signed-16 PCM produced by the C DSP.
It performs no composition or synthesis. Each publication contains the C
snapshot revision and matching `frame_revision` / `audio_revision`, plus a
monotonic bridge sequence that rejects stale display/audio publications. Audio
is a bounded recent interval ending at the snapshot time. Browser audio must be
unlocked by a user gesture; the silent-preview control is also available. The
headless suite writes and checks `output/sim/firmware.wav` independently of the
browser audio device.

The bridge owns an injected monotonic clock, advanced in at most 10 ms steps,
and a bounded message-delivery queue. JSON is parsed only by Python and then
converted to fixed, bounded C event lines. Unknown fields, duplicate keys,
nonfinite/out-of-range values, invalid devices and invalid chains are rejected.
The request limit is 8 KiB, each C command 511 bytes, each C response 128 KiB,
packet delivery 4,096 entries and stdout response deadline 5 seconds. C packets
contain the existing wire protocol, not a Python implementation of sessions.
Fake boot/role/loading readiness is scheduled only by the PC adapter.

The server never resolves filesystem paths supplied by a client: reference
mode allows `/` only; firmware mode additionally allows `/firmware.js` and
`/pcm-worklet.js`. No directory browsing, repository files, Sync directory,
credentials, C runner paths or generated output paths are served. API writes
require the exact local Host and Origin and JSON content type. There is no
hardware transport or hardware injection option in this tool.

Closing or hiding the firmware page stops new clock/input requests and suspends
browser output; it does not enqueue pause, shutdown or reset into a device.
An already accepted bounded request may finish. Actual firmware owns its clock,
radio and audio independently and has no dependency on this bridge. The pinned
reference page keeps its original background-pause behavior. Fault restoration
clears only the selected device's injected conditions and does not re-place or
start devices; battery state is changed separately, as in the reference.

## Verification

```sh
cmake -S pc -B build-host
cmake --build build-host -j8
ctest --test-dir build-host --output-on-failure
python3 tools/echoy-sim/test_sim.py
python3 tools/echoy-sim/run.py --mode firmware --port 8879
# In another terminal, with local Google Chrome and Node.js:
node tools/echoy-sim/browser-check.mjs http://127.0.0.1:8879
```

The Chrome check uses real DevTools mouse, keyboard and touch events. It reads
application observations but does not modify the product state through page
JavaScript. It uses an isolated temporary profile, basic password storage to
avoid Linux desktop-keyring prompts, no proxy and no background networking.
The profile is removed after the test. Reports, screenshots, WAV and logs go
under `output/sim/` and are ignored by Git. `ECHOY_SIM_RUNNER` can point the tests
to an alternate build; CTest sets it to its own target for sanitizer builds.

These tests establish desktop behavior and adapter isolation. Physical magnetic
identity, actual wireless timestamps, speaker/acoustic synchronization,
hardware power behavior, touch latency and 30-minute endurance remain the
hardware acceptance work documented in `docs/echoy/hardware-acceptance.md`.
