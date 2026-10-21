#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bsp="$root/../esp-mosaico-vibe-iris02/submodule/esp-mosaico-bsp/components"
transport_build=$(mktemp -d /tmp/echoy-transport-tests.XXXXXX)
trap 'rm -rf "$transport_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${LINK_TEST_CFLAGS:-} \
 -I"$root/tests/board_link/include" -I"$root/tests/device_runtime/stubs" \
 -I"$root/projects/echoy/main" -I"$bsp/mosaico_network/include" \
 -I"$bsp/mosaico_interaction/include" -I"$bsp/mosaico_peer_link/include" \
 -I"$bsp/esp-mosaico-bsp/include" -include "$root/tests/board_link/include/prelude.h" \
 "$root/tests/board_link/test_transport_window.c" "$root/projects/echoy/main/echoy_link_codec.c" \
 "$root/projects/echoy/main/echoy_music.c" -lm -o "$transport_build/test_transport_window"
"$transport_build/test_transport_window"
