#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
codec_build=$(mktemp -d /tmp/echoy-link-codec.XXXXXX)
trap 'rm -rf "$codec_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 ${LINK_CODEC_TEST_CFLAGS:-} \
    -I"$root/projects/echoy/main" "$root/tests/link_codec/test_codec.c" \
    "$root/projects/echoy/main/echoy_link_codec.c" \
    "$root/projects/echoy/main/echoy_music.c" \
    "$root/projects/echoy/main/echoy_arrange.c" -lm -o "$codec_build/test_codec"
"$codec_build/test_codec"
