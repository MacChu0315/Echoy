#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
group_build=$(mktemp -d /tmp/echoy-group-owner.XXXXXX)
trap 'rm -rf "$group_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${GROUP_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/group/test_boundary.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_audio.c" -lm -o "$group_build/test_boundary"
"$group_build/test_boundary"
