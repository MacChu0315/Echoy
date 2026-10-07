# Echoy for ESP-Mosaico

A fresh ESP-GSP implementation of the frozen HTML in `reference/`.
The initial Chinese Pulse home UI and portable recording model run through the
official native simulator and compile for ESP32-S31. Audio is still simulated.
See [current state](docs/PROJECT_STATE.md) and [screenshots and checks](docs/verification/README.md).

## Local development

The application lives in `projects/echoy`. The sibling `esp-mosaico-vibe`
checkout provides the CLI, utils and pinned BSP. Versions are recorded in
[docs/dependencies.json](docs/dependencies.json). Commands below run from this
repository; `.toolchains` contains the isolated local tools prepared for it:

```sh
export IDF_PATH="$PWD/../.toolchains/esp-idf-7b9cc1a"
export GSPC_EXECUTABLE="$PWD/../.toolchains/gspc-0.6.1/gspc"
export GSP_SIM_EXECUTABLE="$PWD/../.toolchains/gsp-sim-1.5.1/sim"
python3 ../esp-mosaico-vibe/submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/echoy doctor
python3 ../esp-mosaico-vibe/submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/echoy build
python3 ../esp-mosaico-vibe/mosaico.py --workspace . project sim --project projects/echoy --interactive
```

On another machine, prepare the same revisions and point those variables at
their installations. Resolve the manifest with the pinned ESP-IDF build before
running PC preview. Do not substitute an arbitrary latest GSP/compiler pair.

Model and native UI checks:

```sh
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
python3 tools/check_home.py
```

The native check expects the simulator build directory prepared by `project sim`;
it recompiles the backend before testing. It requires Pillow. Scene regeneration
uses Python 3.10+, fontTools, Pillow, CairoSVG and the recorded system font inputs:

```sh
python3 tools/generate_scene.py
```

The HTML is an immutable design/behavior reference, not code executed by GSP.
Follow [AGENTS.md](AGENTS.md) for fidelity, shared implementation and device rules.
