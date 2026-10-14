#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-harmony-voicing.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
flags=""
if [ "${1:-}" = "--sanitize" ]; then
    flags="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie"
    export ASAN_OPTIONS=detect_leaks=1
    export UBSAN_OPTIONS=halt_on_error=1
    shift
fi
cc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG $flags \
  -I"$root/projects/echoy/main" "$root/tests/harmony_voicing/test_harmony_voicing.c" \
  "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_music.c" \
  -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
  -lm -o "$build/test"
"$build/test" "$@"
