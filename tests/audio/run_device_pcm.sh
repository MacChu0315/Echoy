#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-device-pcm.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/projects/echoy/main" \
  "$root/tests/audio/test_device_pcm.c" "$root/projects/echoy/main/echoy_device_pcm.c" \
  -lm -o "$build/test"
"$build/test"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/projects/echoy/main" \
  "$root/tests/audio/test_device_levels.c" "$root/projects/echoy/main/echoy_device_pcm.c" \
  "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_music.c" \
  -lm -o "$build/levels"
"$build/levels"
