#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
upstream_build=$(mktemp -d /tmp/echoy-upstream-synthesis.XXXXXX)
trap 'rm -rf "$upstream_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 ${BAND_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/band/test_upstream_synthesis.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_audio.c" \
 -Wl,--wrap=echoy_audio_prepare -Wl,--wrap=echoy_audio_free_loops \
 -lm -o "$upstream_build/test_upstream_synthesis"
"$upstream_build/test_upstream_synthesis" "$@"
