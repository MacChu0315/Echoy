#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
clock_build=$(mktemp -d /tmp/echoy-peer-clock.XXXXXX)
trap 'rm -rf "$clock_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${PEER_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/linked_device/test_clock_peer.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_pickup.c" -lm -o "$clock_build/test_clock_peer"
"$clock_build/test_clock_peer" "$@"
