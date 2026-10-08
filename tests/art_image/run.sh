#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
image_build=$(mktemp -d /tmp/echoy-art-image-tests.XXXXXX)
trap 'rm -rf "$image_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 \
    ${ART_IMAGE_TEST_CFLAGS:-} \
    -I"$root/projects/echoy/main" \
    -I"$root/projects/echoy/managed_components/espressif__esp-gsp/include" \
    "$root/tests/art_image/test_art_image.c" \
    "$root/projects/echoy/main/echoy_art_image.c" \
    "$root/projects/echoy/main/echoy_art.c" \
    -Wl,--wrap=malloc -Wl,--wrap=free -lm -o "$image_build/test_art_image"
"$image_build/test_art_image" "$image_build/fixtures.bin"
python3 "$root/tests/art_image/check_decode.py" "$image_build/fixtures.bin"
