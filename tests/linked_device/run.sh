#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
peer_build=$(mktemp -d /tmp/echoy-linked-device.XXXXXX)
trap 'rm -rf "$peer_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${PEER_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/linked_device/test_band_peer.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_audio.c" -lm -o "$peer_build/test_peer"
"$peer_build/test_peer"
