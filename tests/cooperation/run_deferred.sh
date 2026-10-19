#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
deferred_build=$(mktemp -d /tmp/echoy-deferred-tests.XXXXXX)
trap 'rm -rf "$deferred_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O1 ${DEFERRED_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/cooperation/test_deferred.c" \
 "$root/projects/echoy/main/echoy_band.c" "$root/projects/echoy/main/echoy_arrange.c" \
 "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
 "$root/projects/echoy/main/echoy_audio.c" "$root/projects/echoy/main/echoy_pickup.c" -lm -o "$deferred_build/test_deferred"
"$deferred_build/test_deferred" "$@"
