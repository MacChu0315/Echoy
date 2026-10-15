#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
group_build=$(mktemp -d /tmp/echoy-group-wire.XXXXXX)
trap 'rm -rf "$group_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${GROUP_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/group/test_protocol.c" \
 "$root/projects/echoy/main/echoy_link_protocol.c" "$root/projects/echoy/main/echoy_link_codec.c" \
 -lm -o "$group_build/test_protocol"
"$group_build/test_protocol"
