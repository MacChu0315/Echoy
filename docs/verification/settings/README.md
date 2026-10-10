# Settings refinement

The user approved four settings entries and explicitly excluded remembering the
last rhythm. The new native GSP pages preserve the paper/green palette and the
home control geometry. Export and Put away are available through the PC desk;
Record and Hear once remain at home and in the role gallery respectively.
The frozen v4 reference is unchanged.

- Settings is one screen: Display, Language, How to play, About this tile.
- The guide is four short steps. About shows device/environment, app version and
  battery; PC readings are explicitly simulated and hardware unknown is `--%`.
- Brightness is 20–100%, default 100%. Idle dimming defaults on: 60 seconds idle
  gives 20%; touch restores the preference. Preparing, recording and playback
  keep the display awake. The current touch continues its usual action.
- Settings are per running tile and reset on restart. No rhythm or preference
  persistence, audio voicing, NVS writes or audio-worker display I/O was added.
- Hardware uses `bsp_display_brightness_set` on the UI owner and the actual app
  description version. A brightness error is reported on the Display page and
  does not silence music. PC uses a native black overlay for a visual preview;
  this does not measure the panel's brightness or power consumption.

## Preview

Open <http://127.0.0.1:3231/> for tile A or <http://127.0.0.1:18899/> for the desk.
A was left at Settings, silent and without a recorded rhythm. Old preview
processes remain alive to preserve the user's recordings. Do not rebuild a live
bundle in place. Restart after its process exits:

```sh
python3 tools/desk_preview.py --build-dir projects/echoy/build-sim-settings \
  --no-build --no-browser --port 18899 --visual-ports 3231,3232,3233
```

The launcher now requests a specific free loopback backend port. GSP 1.5.1's
`--backend-enable` (and an explicit port zero) otherwise searches only
8684–8693, exhausted by the retained previews. No existing preview is stopped.

## Verification

All checks run locally with GSP 1.5.1 and GSPC 0.6.1; these are not physical-device
acceptance. Exact build artifacts are recorded in [artifacts.json](artifacts.json).

- `cmake -S tests -B build-host`, `cmake --build build-host`, and
  `ctest --test-dir build-host --output-on-failure`: recording and display tests
  pass (2/2), including the 60-second boundary, busy reset, touch, off and bounds.
- `python3 tools/generate_scene.py` and official `sim_bridge/build.py` with
  `--project projects/echoy/pc --build-dir projects/echoy/build-sim-settings
  --target echoy_backend --gspc ../.toolchains/gspc-0.6.1/gspc`, followed by
  `cmake --build projects/echoy/build-sim-settings --target echoy_desk --parallel`:
  pass. Final resource compilation has no missing-glyph warnings. Existing font
  sources/metrics are retained; their generated subsets include the approved copy.
- `python3 tools/check_full_ui.py --build-dir projects/echoy/build-sim-settings
  --duration-ms 80000 --output output/settings/ui-verified`: passes the native
  controls, all twelve role/expression combinations, both languages, brightness
  and idle switch, silent recovery, and exact unchanged home/button bounds.
  [Results](ui-checks.json); screenshots in this directory were visually reviewed.
- `python3 tools/check_desk.py --build-dir projects/echoy/build-sim-settings
  --output output/settings/desk-ports`: all three actual C audio/UI owners pass
  native playback, preparation/boundary changes, linkage, detach retention,
  outer desktop Export/Put away/restore, shared language and reduced motion.
  Mixed PCM is nonzero when playing, unclipped and silent after pause.
  This audio regression precedes the final font-charset cleanup; musical/control
  behavior is identical. Its own artifact hashes are in
  [desk-runtime-snapshot.json](desk-runtime-snapshot.json).
- `python3 tests/display/check_idle.py --build-dir projects/echoy/build-sim-settings
  --output output/settings/idle-ready`: passes real elapsed-time checks: 61 seconds
  of playback stay bright, 60 seconds idle dim to 20%, and neutral touch restores
  full brightness without starting music. [Results](idle-checks.json). The initial
  test clicked before the enabled Play control had painted; it now waits for the
  production ready state before input. Final UI and cleanup errors are zero.
- `python3 -m unittest discover -s tests/desk_preview -v`: 12/12 pass with local
  sockets enabled. The sandbox-only attempt could not open sockets; the final
  run uses the same tests with local socket access.
- `python3 tests/device_view/check.py --build-dir output/settings/device-view-build
  --output output/settings/device-view-verified`: real C/GSP fixture passes
  unknown/known battery and all six pages; changing brightness calls the panel
  adapter without shading the hardware framebuffer. Startup submits 20 property
  updates with the configured capacity 64. [Results](device-view-checks.json).
- Pinned IDF low-noise `run_build_step` with arguments
  `-B output/device-audio-ui-build build`: exit 0, compiler warnings 0, run
  `20261010-191200-build-2`. Actual output is the 1,566,576-byte
  `output/device-audio-ui-build/echoy.bin`; the runner's summary points to an older
  default build, so use the artifact paths/hashes in this directory.
- `sha256sum -c SHA256SUMS` inside `reference/v4`: all four frozen files pass.

The new UI has not been installed on hardware. Physical brightness, wake behavior,
power savings and visual acceptance remain to be checked. The currently installed
and accepted speaker settings are unchanged.
