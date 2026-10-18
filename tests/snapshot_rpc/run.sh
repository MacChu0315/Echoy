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
empty, full, bounded = rows[1:4]
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
if len(rows)>4:
    link=rows[4]
    assert link['schema']=='echoy-link-v1' and link['available'] and link['sample_valid']
    assert link['sampled_us']==1234567890123 and link['sequence']==123
    assert link['raw_left']==[-32768,17,623] and link['filtered_left']==[-31000,17,623]
    assert link['classified']==10 and link['contacts']==8 and link['reported']==8
    assert link['peers']==['30eda0f4459a','000000000000'] and link['sessions']==[77,0]
    assert link['group']==[2,1,5] and link['clock_mapped']
    assert link['group_id']=='aabbccdd' and link['attempt']==7
    assert link['radio_admitted'] and link['radio_fresh'] and link['radio_known']==3
    assert link['radio_graph']=='123456789abcdef0' and link['radio_queue_drops']==2
    assert link['timing_us']==dict(membership=-1,**{k:2**63-1 for k in ('started','input_first','input_ready','offered','output_ready','armed','committed','boundary','encoded')})
    transfer=rows[5]
    assert transfer['schema']=='echoy-transfer-v1' and transfer['available'] and transfer['present']
    assert transfer['graph']=='0000000000001234' and transfer['plan']=='0000000000005678'
    assert transfer['queued']==3 and transfer['first_sent']==3 and transfer['acked']==2 and transfer['failed']==1
    assert transfer['retransmitted']==4 and transfer['submit_failures']==1 and transfer['ack_samples']==2
    assert transfer['queue_us_sum']==2**64-1 and transfer['ack_us_sum']==456
    print('PASS diagnostic-only coherent link snapshot and unchanged plan token/lifecycle')
print('PASS trace summary, latest-16/capacity omissions, timestamps, flags and empty trace')
PY
