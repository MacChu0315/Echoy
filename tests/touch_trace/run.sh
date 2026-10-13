#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=$(mktemp -d /tmp/echoy-touch-trace.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
flags=""
if [ "${ECHOY_TOUCH_TRACE_SANITIZE:-0}" = 1 ]; then
    flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
${CC:-cc} -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic $flags -DESP_PLATFORM \
    -I"$root/tests/touch_trace/include" -I"$root/projects/echoy/main" \
    -isystem "$root/projects/echoy/managed_components/espressif__esp_lcd_touch/include" \
    "$root/tests/touch_trace/test_trace.c" "$root/projects/echoy/main/board_touch_trace.c" \
    -pthread -o "$build_dir/test_trace"
"$build_dir/test_trace"
"$build_dir/test_trace" --dump > "$build_dir/dump.log"
# 120 input callbacks stay silent; explicit dump is bounded to 64 event rows
# plus two summary lines and the omission/interpretation line.
test "$(wc -l < "$build_dir/dump.log")" -eq 67
${CC:-cc} -std=c11 -O1 -Wall -Wextra -Werror -Wpedantic \
    -I"$root/projects/echoy/main" "$root/tests/touch_trace/test_native.c" \
    "$root/projects/echoy/main/board_touch_trace.c" -o "$build_dir/test_native"
"$build_dir/test_native"
printf '%s\n' 'PASS: explicit dump output capped at 67 lines; native UI header has no ESP dependency'
