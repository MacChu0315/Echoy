#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-snapshot-rpc.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror -DESP_PLATFORM ${SNAPSHOT_TEST_CFLAGS:-} \
  -fstack-usage -Wstack-usage=1024 -Dfree=snapshot_test_free \
  -I"$root/tests/snapshot_rpc/stubs" -I"$root/tests/audio_rpc/stubs" -I"$root/tests/device_runtime/stubs" \
  -I"$root/projects/echoy/main" -c \
  "${ECHOY_SNAPSHOT_SOURCE:-$root/projects/echoy/main/echoy_snapshot_diagnostics.c}" -o "$build/snapshot.o"
cc -std=c11 -O2 -Wall -Wextra -Werror -DESP_PLATFORM ${SNAPSHOT_TEST_CFLAGS:-} \
  -I"$root/tests/snapshot_rpc/stubs" -I"$root/tests/audio_rpc/stubs" -I"$root/tests/device_runtime/stubs" \
  -I"$root/projects/echoy/main" "$root/tests/snapshot_rpc/test_rpc.c" \
  "$root/projects/echoy/main/echoy_music.c" "$root/projects/echoy/main/echoy_link_codec.c" \
  "$build/snapshot.o" -lm -o "$build/test"
"$build/test" > "$build/reply.json"
python3 - "$build/reply.json" <<'PY'
import json, sys
rows = [json.loads(line) for line in open(sys.argv[1])]
d = rows[0]
assert d['volume'] == .34234234234234234
assert d['phase'] == 5 and d['generation'] == 9
assert d['revision'] == 12 and d['committed_revision'] == 11 and d['plan_pending'] is True
assert d['plan_role'] == 3 and d['plan_phrase'] == 2 and d['source'] == 1
assert d['size'] > 512 and len(d['token']) == 16 and len(d['crc32']) == 8
print('PASS exact volume and coherent phase/revision/plan metadata JSON')
empty, full, bounded = rows[1:]
assert empty['available'] and empty['edges'] == [] and empty['pressed_omitted'] == 0
assert empty['state'] == 0
assert full['schema'] == 'echoy-touch-v1' and full['available']
assert full['state'] == 3
assert full['summary'][14:16] == [7, 3]  # overwritten and busy drops
assert full['pressed_retained'] == 20 and len(full['edges']) == 16
assert full['pressed_omitted'] == 9 and full['events_not_returned'] == 24 and full['irqs_not_returned'] == 64
assert full['edges'][0][4] == 9 and full['edges'][-1][4] == 39
assert full['edges'][-1][0:4] == [1234567890161, 1234567890038, 1234567890138, 1234567000000]
assert full['edges'][-1][8:] == [2, 6, 0x4001, 1, 1, 1]
assert 0 < len(bounded['edges']) < 16 and bounded['edges'][-1][4] == 39
assert bounded['pressed_omitted'] == 25 - len(bounded['edges'])
assert bounded['events_not_returned'] == 40 - len(bounded['edges'])
print('PASS trace summary, latest-16/capacity omissions, timestamps, flags and empty trace')
PY
