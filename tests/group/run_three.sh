#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
group_build=$(mktemp -d /tmp/echoy-three.XXXXXX)
trap 'rm -rf "$group_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${GROUP_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/group/test_three.c" \
 "$root/projects/echoy/main/echoy_group.c" "$root/projects/echoy/main/echoy_link_protocol.c" \
 "$root/projects/echoy/main/echoy_link_codec.c" "$root/projects/echoy/main/echoy_band.c" \
 "$root/projects/echoy/main/echoy_arrange.c" "$root/projects/echoy/main/echoy_model.c" \
 "$root/projects/echoy/main/echoy_music.c" "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_pickup.c" \
 -lm -o "$group_build/test_three"
"$group_build/test_three" "$@"
