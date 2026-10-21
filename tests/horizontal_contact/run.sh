#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bsp="$root/../esp-mosaico-vibe-iris02/submodule/esp-mosaico-bsp/components"
contact_build=$(mktemp -d /tmp/echoy-horizontal.XXXXXX)
trap 'rm -rf "$contact_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -g -O1 ${CONTACT_TEST_CFLAGS:-} \
 -I"$root/tests/horizontal_contact/include" -I"$root/tests/board_link/include" -I"$root/tests/device_runtime/stubs" \
 -I"$root/projects/echoy/main" -I"$bsp/mosaico_network/include" -I"$bsp/mosaico_interaction/include" \
 -I"$bsp/mosaico_peer_link/include" -I"$bsp/esp-mosaico-bsp/include" \
 -include "$root/tests/board_link/include/prelude.h" \
 "$root/tests/horizontal_contact/test_contact.c" "$bsp/mosaico_interaction/mosaico_mag_classifier.c" \
 "$root/projects/echoy/main/echoy_radio.c" "$root/projects/echoy/main/echoy_group.c" "$root/projects/echoy/main/echoy_link_protocol.c" \
 "$root/projects/echoy/main/echoy_link_codec.c" "$root/projects/echoy/main/echoy_music.c" \
 -lm -o "$contact_build/test_contact"
"$contact_build/test_contact"
