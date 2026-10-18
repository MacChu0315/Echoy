#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-commit-pressure.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -O1 ${GROUP_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/group/test_commit_pressure.c" \
 "$root/projects/echoy/main/echoy_radio.c" "$root/projects/echoy/main/echoy_group.c" \
 "$root/projects/echoy/main/echoy_link_protocol.c" "$root/projects/echoy/main/echoy_link_codec.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_pickup.c" -lm -o "$build/test_pressure"
"$build/test_pressure"
