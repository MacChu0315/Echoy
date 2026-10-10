#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
audio_build=$(mktemp -d /tmp/echoy-audio-tests.XXXXXX)
trap 'rm -rf "$audio_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 ${AUDIO_TEST_CFLAGS:-} \
    -I"$root/projects/echoy/main" "$root/tests/audio/test_audio.c" \
    "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_music.c" \
    -lm -o "$audio_build/test_audio"
"$audio_build/test_audio"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 -UNDEBUG ${AUDIO_TEST_CFLAGS:-} \
    -I"$root/projects/echoy/main" "$root/tests/audio/test_failures.c" \
    "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_music.c" \
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free -lm -o "$audio_build/test_failures"
"$audio_build/test_failures"
sh "$root/tests/audio/run_performance.sh"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 -UNDEBUG ${AUDIO_TEST_CFLAGS:-} \
    -I"$root/projects/echoy/main" "$root/tests/audio/test_filter_extremes.c" \
    "$root/tests/audio/baseline_probe.c" "$root/projects/echoy/main/echoy_audio.c" \
    -lm -o "$audio_build/test_filter_extremes"
"$audio_build/test_filter_extremes"
