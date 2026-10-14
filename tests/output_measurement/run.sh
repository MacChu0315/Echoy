#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-output-measurement.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
flags=""
if [ "${1:-}" = "--sanitize" ]; then
    flags="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie"
    export ASAN_OPTIONS=detect_leaks=1
    export UBSAN_OPTIONS=halt_on_error=1
    shift
fi
# Intentional word splitting for compiler flags.
cc -std=c11 -O2 -Wall -Wextra -Werror $flags -I"$root/projects/echoy/main" \
    "$root/tests/output_measurement/test_output_measurement.c" \
    "$root/projects/echoy/main/echoy_output_measurement.c" -lm -o "$build/test"
"$build/test" "$@"
