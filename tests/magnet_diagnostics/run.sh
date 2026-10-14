#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bsp="$root/../esp-mosaico-vibe/submodule/esp-mosaico-bsp/components"
mag_build=$(mktemp -d /tmp/echoy-mag-tests.XXXXXX)
trap 'rm -rf "$mag_build"' EXIT HUP INT TERM
for profile in enabled disabled; do
    define=
    if [ "$profile" = enabled ]; then define=-DCONFIG_ECHOY_MAGNET_DIAGNOSTICS=1; fi
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${MAG_TEST_CFLAGS:-} $define \
      -I"$root/tests/board_link/include" -I"$root/tests/device_runtime/stubs" \
      -I"$root/projects/echoy/main" -I"$bsp/mosaico_network/include" \
      -I"$bsp/mosaico_interaction/include" -I"$bsp/mosaico_peer_link/include" \
      -I"$bsp/esp-mosaico-bsp/include" -include "$root/tests/board_link/include/prelude.h" \
      "$root/tests/magnet_diagnostics/test_diagnostics.c" "$root/projects/echoy/main/echoy_link_codec.c" \
      "$root/projects/echoy/main/echoy_music.c" -lm -o "$mag_build/$profile"
    "$mag_build/$profile"
done
