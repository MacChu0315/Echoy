#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
io_build=$(mktemp -d /tmp/echoy-desk-io.XXXXXX)
trap 'rm -rf "$io_build"' EXIT HUP INT TERM
component="$root/projects/echoy/managed_components/espressif__esp-gsp"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections \
 -I"$root/projects/echoy/main" -I"$component/tools/sim_bridge/include" -I"$component/include" \
 "$root/tests/desk_runtime/test_io.c" "$root/projects/echoy/main/echoy_band.c" \
 "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_pickup.c" "$root/projects/echoy/main/echoy_arrange.c" \
 -Wl,--gc-sections -lpthread -lm -o "$io_build/test_io"
"$io_build/test_io"
