#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bsp="$root/../esp-mosaico-vibe/submodule/esp-mosaico-bsp/components"
link_build=$(mktemp -d /tmp/echoy-link-tests.XXXXXX)
trap 'rm -rf "$link_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${LINK_TEST_CFLAGS:-} \
 -I"$root/tests/board_link/include" -I"$root/tests/device_runtime/stubs" \
 -I"$root/projects/echoy/main" -I"$bsp/mosaico_network/include" \
 -I"$bsp/mosaico_interaction/include" -I"$bsp/mosaico_peer_link/include" \
 -I"$bsp/esp-mosaico-bsp/include" -include "$root/tests/board_link/include/prelude.h" \
 "$root/tests/board_link/test_board_link.c" "$root/projects/echoy/main/echoy_link_codec.c" \
 "$root/projects/echoy/main/echoy_music.c" -lm -o "$link_build/test_board_link"
"$link_build/test_board_link"

sh "$root/tests/board_link/run_transport_window.sh"
