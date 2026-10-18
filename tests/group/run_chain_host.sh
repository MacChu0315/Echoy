#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
group_build=$(mktemp -d /tmp/echoy-chain-host.XXXXXX)
trap 'rm -rf "$group_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${GROUP_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" -I"$root/projects/echoy/pc" \
 "$root/tests/group/test_chain_host.c" "$root/projects/echoy/pc/chain_owner.c" \
 "$root/projects/echoy/main/echoy_radio.c" "$root/projects/echoy/main/echoy_group.c" "$root/projects/echoy/main/echoy_link_protocol.c" \
 "$root/projects/echoy/main/echoy_link_codec.c" "$root/projects/echoy/main/echoy_band.c" \
 "$root/projects/echoy/main/echoy_arrange.c" "$root/projects/echoy/main/echoy_model.c" \
 "$root/projects/echoy/main/echoy_music.c" "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_pickup.c" \
 -lm -o "$group_build/test_chain_host"
"$group_build/test_chain_host"
