#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-speaker-band.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 ${BAND_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/band/test_speaker_voicing.c" \
 "$root/projects/echoy/main/echoy_model.c" \
 "$root/projects/echoy/main/echoy_music.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_audio.c" -Wl,--wrap=echoy_audio_prepare \
 -Wl,--wrap=echoy_audio_free_loops -Wl,--wrap=echoy_audio_voice_for_speaker -lm -o "$build/test"
"$build/test"
