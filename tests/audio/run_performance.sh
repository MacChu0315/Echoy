#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
audio_build=$(mktemp -d /tmp/echoy-audio-performance.XXXXXX)
trap 'rm -rf "$audio_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -UNDEBUG -fno-builtin ${AUDIO_TEST_CFLAGS:-} \
    -I"$root/projects/echoy/main" "$root/tests/audio/test_performance.c" \
    "${ECHOY_AUDIO_SOURCE:-$root/projects/echoy/main/echoy_audio.c}" "$root/projects/echoy/main/echoy_music.c" \
    -Wl,--wrap=sin -Wl,--wrap=cos -Wl,--wrap=exp -Wl,--wrap=exp2 -Wl,--wrap=sqrt \
    -Wl,--wrap=pow -Wl,--wrap=powf -Wl,--wrap=sinf -Wl,--wrap=cosf -lm -o "$audio_build/test_performance"
"$audio_build/test_performance"
