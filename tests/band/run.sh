#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
band_build=$(mktemp -d /tmp/echoy-band-tests.XXXXXX)
trap 'rm -rf "$band_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 ${BAND_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/band/test_band.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_model.c" \
 "$root/projects/echoy/main/echoy_music.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_audio.c" -lm -o "$band_build/test_band"
"$band_build/test_band"

sh "$root/tests/band/run_calibration.sh"
sh "$root/tests/band/run_upstream_synthesis.sh"
sh "$root/tests/band/run_harmony_bass.sh"
