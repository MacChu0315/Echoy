# Read-only current-plan snapshots

The application registers private service `0xECA1` after successful startup.
It does not enable the optional `0xECA0` reference-tone service. Plan methods
only observe `platform.read_control`'s coherent cached view: they never call
the band, input actions, audio hardware or playback/stop functions. The PCM
publisher retains its existing zero-wait cache publication behavior.

Magnetic diagnostic builds also register method 4 (empty payload). It returns
`echoy-link-v1` with `available:false` when the worker is inactive, has not sampled,
or its cache is busy. Otherwise it copies the worker's latest sample sequence,
local `sampled_us`, validity/readiness, compensated and filtered axes, sample
count, calibration state, classified/debounced/reported BSP masks, admitted
peers/sessions, group count/rank/phase and mapped-clock flag. Mask bits are TOP1,
RIGHT2, BOTTOM4, LEFT8. All axes are invalid when `sample_valid` is false; do not
interpret zero-filled failed reads as disconnection measurements. Compare the
sample timestamp with the same boot's uptime before drawing freshness conclusions.
The RPC never samples sensors, sends network packets, or replaces a plan token.
`radio_admitted`/`radio_fresh` distinguish a usable route from retained physical
peers during recovery; `radio_known`, graph, submit-failure/drop counters and
group identity/attempt permit coherent interpretation. A DONE phase alone is
not proof of radio availability. The copied structure uses bounded temporary
PSRAM; allocation failure returns `available:false/reason:no_psram` without
reading the cache. All paths release the copy and preserve the stack limit.
Its complete JSON fits the existing1024-byte Iris response. It is absent from
ordinary builds. Test both profiles with `sh tests/snapshot_rpc/run.sh` and
`SNAPSHOT_TEST_CFLAGS=-DCONFIG_ECHOY_MAGNET_DIAGNOSTICS=1 sh tests/snapshot_rpc/run.sh`.

- Method 1 takes an empty payload. It reads one complete view and encodes its
  current `plan` using the production ECHP codec. The JSON reply includes a
  16-digit hexadecimal token, byte size, hexadecimal CRC32, exact UI target
  volume, phase, generation, requested/committed revisions, pending/preparing
  flags, current plan role/phrase/density, source and clock information.
- Method 2 takes exactly 14 bytes: little-endian `u64 token`, `u32 offset`,
  `u16 length`. It returns 1–512 raw immutable ECHP bytes. Out-of-bounds offsets,
  stale tokens, insufficient output capacity and overlapping handlers fail.
  Capture allocates two complete view/8192-byte encoded slots explicitly in
  PSRAM. The current codec's largest legal plan is 4208 bytes; the transport and
  storage retain its declared 8192-byte ceiling. No large view, ECHP payload or
  metadata temporary is placed on the Iris worker stack.

Method 3 takes an empty payload and reads `board_touch_trace_snapshot` once.
Its temporary summary/512 event/64 IRQ storage is allocated explicitly in
PSRAM and freed after the reply. It performs no extra touch reads, logging dump,
input action, pause or playback. Missing attachment or a busy producer returns
`{"schema":"echoy-touch-v1","available":false,"reason":"inactive_or_busy"}`;
PSRAM allocation failure returns the same unavailable shape with `no_psram`.
The producer's boolean API cannot distinguish inactive from busy. An overlapping
RPC handler itself returns `ESP_ERR_INVALID_STATE` immediately.

The available response has schema `echoy-touch-v1`, a `summary` numeric array,
and `edges` containing the latest **at most 16** observer press edges, in sequence
order. Each row must have `kind=OBSERVER`, `pressed=true`, `edge=1` at the source;
initial/unchanged samples and raw-driver press rows are excluded. Formatting
uses at most `min(response_capacity,3500)` bytes, including its NUL terminator.
The installed Iris configuration offers only 1024 bytes, so fewer than 16 rows
may fit. Only the same copied snapshot is reformatted; touch is never reread.
A response too small for the complete summary returns `ESP_ERR_INVALID_SIZE`.
Top-level `state` is 0 inactive, 1 armed after Record, 2 capturing, or 3 frozen
after capture/cancel. This preserves the producer's recording-scoped lifetime;
reading the snapshot never arms, resets or freezes it.

The 24 `summary` positions, in order, are:

```text
irq_count, irq_overwritten, reads, read_errors, raw_reports,
raw_presses, raw_releases, observers, observer_presses, submitted,
same_grid, multiple_irq_windows, irq_during_read, event_count,
event_overwritten, busy_drops, events_copied, events_omitted,
irqs_copied, irqs_omitted, max_read_gap_us, max_read_duration_us,
max_latest_irq_age_us, max_raw_to_observer_us
```

The 14 positions of each `edges` row are:

```text
at_us, irq_at_us, raw_at_us, capture_at_us, seq, read_seq, raw_seq,
irq_seq, phase, grid, pending_seed, submitted, raw_match, same_grid
```

`pressed_retained` is the number of matching press edges in the copied ring.
`pressed_omitted` is total recorded observer presses minus returned rows,
including overwritten presses and rows dropped to fit the reply. Producer
`event_overwritten`, `irq_overwritten`, `busy_drops`, `events_omitted` and
`irqs_omitted` remain explicitly present in the summary. `events_not_returned`
is copied event count minus returned edges; `irqs_not_returned` is the copied
IRQ count, since raw IRQ rows are intentionally not returned. These categories
overlap; they must not be summed into a claimed lost-touch count. Method 3 does
not read/change the plan snapshot, token, volume or playback state.

Read the trace through the official CLI, verifying the same live device and
boot before and after a single method-3 call:

```sh
python3 tools/capture_touch_trace.py output/current-touch \
  --device-id 4553502d49524953010030eda0f40c6c
```

The new-directory output `touch.json` expands both arrays into named fields
without losing 64-bit integer timestamp precision and preserves all omission
counts. Unavailable/malformed results or boot changes fail without publishing
a capture. The script never retries or invokes touch/audio control methods.

Only a successful new capture replaces the active snapshot. Failed cache
reads, invalid plans/volumes and undersized response buffers leave the previous
token readable. A new capture invalidates older tokens, so concurrent clients
cannot silently assemble different snapshots. Tokens are not reused across
stop/start within a firmware boot. They are observation identifiers, not access
credentials. The host additionally verifies that the device boot stays the same.

`echoy_snapshot_diagnostics_stop` disables callbacks before attempting removal.
The pinned public Iris unregister API returns busy while an executor could
still own the callback context. Registration rollback and stop therefore keep
the handle/storage alive on such failure; callers must retry stop before
releasing the platform context. In `app_main`, registration follows all
fallible application initialization and cleanup. The running device context
then remains alive until reset. Diagnostic allocation/registration failure is
logged without changing playback or product UI.

Capture from an already-installed version exposing ECA1 through the official
workflow:

```sh
python3 tools/capture_audio_snapshot.py output/current-harmony \
  --device-id 4553502d49524953010030eda0f40c6c
```

The script requires a new output directory and uses only official
`mosaico.py --json iris device-status` and `iris rpc` commands. The pinned
wrapper is the default; `--mosaico` and `--project` can select an existing
official installation/project. It does not access private Gateway HTTP routes,
claim a different identity, restart, install, play sound or enable tone tests.
The CLI's existing session selection rules still apply. It checks live identity
and boot before/after, validates every chunk and the complete CRC, then saves
`plan.echp` and `snapshot.json`. A stale-token/RPC failure stops immediately
without automatic retry or publishing a partial capture.

Feed `plan.echp` to `tests/audio/probe_stages.py --plan-bin` and use the exact
`metadata.volume` from `snapshot.json`. The plan is the coherent cached current
plan, with full linked pitches/events; pending flags distinguish a requested
change from the captured committed plan. Volume is the exact target value,
not a sample-by-sample smoothed PCM gain. This is suitable for unchanged steady
playback; it does not capture transitions, concurrent banks or audio timing.

Checks:

```sh
sh tests/snapshot_rpc/run.sh
python3 tests/snapshot_rpc/test_capture.py
python3 tests/snapshot_rpc/test_touch_capture.py
SNAPSHOT_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' sh tests/snapshot_rpc/run.sh
sh tests/audio_rpc/run.sh
python3 tests/device_runtime/test_runtime.py
```

The C suite covers immutable cached-plan/ECHP round trips, maximum legal plans,
exact volume/metadata, failed-capture preservation, stale tokens, bounded chunks,
PSRAM allocation failure, concurrent reentry and partial registration/shutdown.
Its compilation rejects own stack usage above 1024 bytes. This static bound
does not account for libc call depth; firmware stack-watermark observation is
still required. The Python tests exercise a controlled official CLI boundary,
including complete 8192-byte transfer, corruption, boot changes, RPC failure
without retries and preservation of existing outputs. They do not contact a
device. ASan/UBSan and default leak detection passed outside the ptrace-based
runner; use that environment if LeakSanitizer cannot inspect threads.

Development checked the official CLI help and pinned Iris public API/source.
No device operation was performed by the implementation/test task. Firmware
build, physical installation and live snapshot capture remain separate checks.
