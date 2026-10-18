#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bsp="$root/../esp-mosaico-vibe/submodule/esp-mosaico-bsp/components"
transaction_build=$(mktemp -d /tmp/echoy-transaction-tests.XXXXXX)
trap 'rm -rf "$transaction_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${TRANSACTION_TEST_CFLAGS:-} \
 -I"$root/tests/board_link/include" -I"$root/tests/device_runtime/stubs" \
 -I"$root/projects/echoy/main" -I"$bsp/mosaico_network/include" \
 -I"$bsp/mosaico_interaction/include" -I"$bsp/mosaico_peer_link/include" \
 -I"$bsp/esp-mosaico-bsp/include" -include "$root/tests/board_link/include/prelude.h" \
 "$root/tests/cooperation/test_transactions.c" "$root/projects/echoy/main/echoy_link_codec.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_pickup.c" -lm -o "$transaction_build/test_transactions"
"$transaction_build/test_transactions" "$@"
