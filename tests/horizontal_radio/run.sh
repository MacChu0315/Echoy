#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-radio.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -O1 ${RADIO_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/horizontal_radio/test_radio.c" \
 "$root/projects/echoy/main/echoy_radio.c" -o "$build/test_radio"
"$build/test_radio"
