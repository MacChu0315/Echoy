#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
render_build=$(mktemp -d /tmp/echoy-render-active.XXXXXX)
trap 'rm -rf "$render_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -g ${RENDER_TEST_CFLAGS:-} -I"$root/projects/echoy/main" \
 "$root/tests/render_active/test_active.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_pickup.c" -lm -o "$render_build/test_active"
"$render_build/test_active"
