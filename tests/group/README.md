# Horizontal group verification

The v3 group and both-seam firmware adapter are implemented. Use the delivery record in
`docs/verification/three-device-horizontal/` for the verified candidate/status.
`run_owner.sh` tests the actual band and PCM at an immutable, in-block boundary:
old current phrase starts promptly after ARM, both changed and unchanged banks
align there, and paused participants remain exactly silent. The band's explicit
`align_boundary` owner contract opts into this v3 behavior; the accepted v2
adapter does not set it.

`run_protocol.sh` exercises the portable coverage/CRC, one-edge clock sampling
and bounded control/data slot primitives. `run_three.sh` creates three separately
allocated real group/band/preparation/PCM owners, replacing only physical edges,
clock offsets and network transport. It covers all six ID orders and both join
orders, independent first starts, simultaneous starts, middle edits after READY,
fixed-update overlap and physical release retention. Further regressions cover
unmapped simultaneous starts, fresh clock windows, delayed owner publications,
70/140-second boot offsets in both directions with bounded unmapped timeouts,
superseded upstream candidates, radio loss/session renewal, failed preparation,
loading-to-normal recovery, missing/v2 peers, empty first recording, and a middle
node finishing capture/reply before admitting its upstream edge.

`run_adapter.sh` compiles the active firmware adapter against hardware substitutes
and checks both seams, radio-versus-physical release and stop failure ownership.
`run_chain_host.sh` validates the independent native adapter with actual PCM.
`tools/check_three_chain.py` drives three official GSP hosts through real C pointer
callbacks, independent clocks and owners, then checks captured PCM and natural
cleanup. Run it alone in normal/reduced motion; these host checks do not establish
physical timing or listening acceptance.

The prior v2 adapter is retained only in `tests/board_link/v2_baseline.inc` as the
explicit historical fixture for existing transport/transaction regressions. It
is excluded from firmware. Active v3 behavior is checked by the group suites.

Use `GROUP_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`
for sanitizer runs. LeakSanitizer requires execution outside the ptrace sandbox.
