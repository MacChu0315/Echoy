#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
preview_build=$(mktemp -d /tmp/echoy-pad-preview.XXXXXX)
trap 'rm -rf "$preview_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 ${BAND_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/band/test_pad_preview.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_model.c" \
 "$root/projects/echoy/main/echoy_music.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_audio.c" -lm -o "$preview_build/test_pad_preview"
"$preview_build/test_pad_preview"
