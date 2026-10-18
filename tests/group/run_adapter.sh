#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bsp="$root/../esp-mosaico-vibe/submodule/esp-mosaico-bsp/components"
group_build=$(mktemp -d /tmp/echoy-group-adapter.XXXXXX)
trap 'rm -rf "$group_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${GROUP_TEST_CFLAGS:-} \
 -I"$root/tests/board_link/include" -I"$root/tests/device_runtime/stubs" \
 -I"$root/projects/echoy/main" -I"$bsp/mosaico_network/include" \
 -I"$bsp/mosaico_interaction/include" -I"$bsp/mosaico_peer_link/include" \
 -I"$bsp/esp-mosaico-bsp/include" -include "$root/tests/board_link/include/prelude.h" \
 "$root/tests/group/test_adapter.c" "$root/projects/echoy/main/echoy_radio.c" "$root/projects/echoy/main/echoy_group.c" \
 "$root/projects/echoy/main/echoy_link_protocol.c" "$root/projects/echoy/main/echoy_link_codec.c" \
 -lm -o "$group_build/test_adapter"
"$group_build/test_adapter"
