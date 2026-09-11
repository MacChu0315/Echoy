# Portable music composition

`components/echoy_core` ports the executable reference's twelve phrases to C11.
`echoy_music_compose()` accepts a role, phrase 1–3, committed activity, nonnegative
bar, duplicate index 0–2, positive finite BPM, and the complete direct-left motif.
There are no volume, device identity, right-neighbor, transport, or chain-head
inputs. Session code owns routing and duplicate counts. Compose never modifies
its left input; output aliasing that motif is rejected.

Motifs contain at most 16 events; notes/harmony contain at most three pitches,
as produced by the prototype. All rule arithmetic uses `double`. Activity is
clamped to [0,1], with nonfinite activity mapped to 0. Invalid role, phrase,
duplicate, bar, BPM, counts, or kinds return false. A false result clears output,
except when output aliases the left input. Analysis skips nonfinite/out-of-bar
steps and nonpositive/nonfinite durations, clamps velocity, and retains weak
notes. A missing pitched note is represented by `NAN`. Set `has_notes` and
`has_harmony` to preserve the difference between absent and empty arrays.
Initialize event storage to zero and use chord -1 for absent chord metadata.

`phrase.c`, `response.c`, and `harmony.c` correspond to the extracted engine,
response, and harmony modules. Negative wrap, JavaScript rounding, stable event
order, and first-candidate tie selection are explicit. The harmony search scans
all 66 candidates in the original iteration order. The versioned input is
`tools/harmony-voicings.json`; regenerate its C table with
`python3 tools/generate_voicings.py`, or verify with `--check`.

## Host comparison

```sh
cmake -S pc -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure -R music
python3 tests/compare_oracle.py --runner build-host/echoy_core_runner
```

The runner accepts one object per newline on stdin:

```json
{"role":"voice","phrase":1,"activity":0.56,"bar":0,"duplicate":0,"bpm":96,"motif":null}
```

Replace `motif` with an array of prototype event objects to supply a left motif.
Output is one array of event tuples per input line, in the `eventFields` order
stored in `tests/cases/music-fixtures.json`. Invalid input prints a line-numbered
diagnostic to stderr and exits 2. Input lines must end in a newline and fit in
16 KiB; tokens, nesting, motifs, and pitch arrays are bounded. This parser is a
host adapter, not part of the firmware core.

The comparator only reads committed expected results. It checks all 48,600
original cases, then 48 three-device chains (feeding native results into the next
native device) and 360 edge cases. The additional fixtures come from the pinned
HTML, independently of C, using `node tools/generate_music_edges.mjs`; `--check`
verifies them without rewriting them. The original Task 1 fixtures are unchanged.

For address/undefined-behavior checks:

```sh
cmake -S pc -B /tmp/echoy-task3-asan \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -g' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined -no-pie'
cmake --build /tmp/echoy-task3-asan
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/echoy-task3-asan --output-on-failure -R music
```

Leak detection is disabled in this command because this workspace's traced
execution environment prevents LeakSanitizer from starting. The composition
core performs no dynamic allocation. Host equivalence does not establish MCU
runtime, stack usage, timing, synthesis, or audio acceptance.
