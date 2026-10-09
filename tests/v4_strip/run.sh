#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
strip_build=$(mktemp -d /tmp/echoy-v4-strip.XXXXXX)
trap 'rm -rf "$strip_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 ${STRIP_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/v4_strip/test_strip.c" \
 "$root/projects/echoy/main/echoy_strip.c" "$root/projects/echoy/main/echoy_model.c" \
 "$root/projects/echoy/main/echoy_music.c" -lm -o "$strip_build/test_strip"
"$strip_build/test_strip"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 ${STRIP_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/v4_strip/dump_glyphs.c" \
 "$root/projects/echoy/main/echoy_strip_image.c" -o "$strip_build/dump_glyphs"
"$strip_build/dump_glyphs" "$strip_build"
python3 "$root/tests/v4_strip/check_glyphs.py" "$strip_build"
