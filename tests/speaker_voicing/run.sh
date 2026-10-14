#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-speaker-voicing.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
flags=""
if [ "${1:-}" = "--sanitize" ]; then
    flags="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie"
    export ASAN_OPTIONS=detect_leaks=1
    export UBSAN_OPTIONS=halt_on_error=1
fi
cc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG $flags \
 -I"$root/projects/echoy/main" "$root/tests/speaker_voicing/test_speaker_voicing.c" \
 "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_device_pcm.c" "$root/projects/echoy/main/echoy_link_codec.c" \
 -Wl,--wrap=malloc -lm -o "$build/test"
"$build/test" "$root/docs/verification/harmony-bass/residual-plan.echp"
