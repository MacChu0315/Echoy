#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-audio-rpc.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/tests/audio_rpc/stubs" \
  -I"$root/tests/device_runtime/stubs" -I"$root/projects/echoy/main" \
  "$root/tests/audio_rpc/test_rpc.c" "$root/projects/echoy/main/echoy_audio_diagnostics.c" \
  -o "$build/test"
"$build/test" > "$build/reply.json"
python3 - "$build/reply.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
assert d['phase']==3 and d['generation']==6 and d['requested_generation']==7
assert d['frames']==24000 and d['frequency_hz']==1000 and d['peak_pcm']==8192
assert d['hardware_volume']==90 and d['ui_volume']==100
assert d['before']['regs']==[128,63,12,96,188,131,17]
assert d['after']['regs']==[128,63,12,0,188,131,17]
assert d['output']['nonzero']==23000
print('PASS complete JSON status and unambiguous queued-test generation')
PY
