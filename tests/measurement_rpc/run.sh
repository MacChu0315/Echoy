#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-measurement-rpc.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror ${MEASUREMENT_RPC_CFLAGS:-} \
  -fstack-usage -Wstack-usage=1024 \
  -I"$root/tests/audio_rpc/stubs" -I"$root/tests/device_runtime/stubs" \
  -I"$root/projects/echoy/main" -c "$root/projects/echoy/main/echoy_measurement_diagnostics.c" \
  -o "$build/rpc.o"
cc -std=c11 -O2 -Wall -Wextra -Werror ${MEASUREMENT_RPC_CFLAGS:-} \
  -I"$root/tests/audio_rpc/stubs" -I"$root/tests/device_runtime/stubs" \
  -I"$root/projects/echoy/main" "$root/tests/measurement_rpc/test_rpc.c" "$build/rpc.o" \
  -o "$build/test"
"$build/test" > "$build/replies.jsonl"
python3 - "$build/replies.jsonl" <<'PY'
import json,sys
s,summary,tone=map(json.loads,open(sys.argv[1]))
assert s['schema']==1 and s['generation']==7 and s['runs_remaining']==1
assert s['frames_captured']==216000 and s['elapsed_us']==9000123 and s['input_gain_db']==18
assert summary['run_id']==7 and summary['tones_analyzed']==15 and summary['noise']['sample_count']==7200
assert tone['run_id']==7 and tone['index']==0 and tone['frequency_hz']==220
assert abs(tone['harmonic_ratio'][0]-.1)<1e-6 and 'excerpt' not in tone
print('PASS measurement RPC explicit starts, capacity/shape/gain/run guards, compact JSON and local-only metrics')
PY
