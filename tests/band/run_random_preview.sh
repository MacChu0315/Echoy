#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
random_build=$(mktemp -d /tmp/echoy-random-preview.XXXXXX)
trap 'rm -rf "$random_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 ${BAND_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/band/test_random_preview.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_model.c" \
 "$root/projects/echoy/main/echoy_music.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_device_pcm.c" -lm -o "$random_build/test_random_preview"
"$random_build/test_random_preview"
