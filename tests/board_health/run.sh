#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build_dir=$(mktemp -d /tmp/echoy-health-tests.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
flags=""
if [ "${ECHOY_HEALTH_SANITIZE:-0}" = 1 ]; then
    flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
${CC:-cc} -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic $flags -DESP_PLATFORM \
    -I tests/board_health/include -I projects/echoy/main \
    tests/board_health/test_health.c projects/echoy/main/board_health.c \
    -lm -pthread -o "$build_dir/test_health"
"$build_dir/test_health"
