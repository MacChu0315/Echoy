#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
art_build=$(mktemp -d /tmp/echoy-art-tests.XXXXXX)
trap 'rm -rf "$art_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 \
    ${ART_TEST_CFLAGS:-} \
    -I"$root/projects/echoy/main" \
    -I"$root/projects/echoy/managed_components/espressif__esp-gsp/include" \
    "$root/tests/art/test_art.c" "$root/projects/echoy/main/echoy_art.c" \
    -lm -o "$art_build/test_art"
"$art_build/test_art"
