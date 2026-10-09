#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-audio-probe.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/projects/echoy/main" \
  "$root/tests/audio/test_probe.c" "$root/projects/echoy/main/echoy_audio_probe.c" -o "$build/test"
"$build/test"
