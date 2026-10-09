#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
board_build=$(mktemp -d /tmp/echoy-board-audio-tests.XXXXXX)
trap 'rm -rf "$board_build"' EXIT HUP INT TERM
codec="$root/projects/echoy/managed_components/espressif__esp_codec_dev"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wpedantic -UNDEBUG -g -O1 \
    ${BOARD_AUDIO_TEST_CFLAGS:-} -DCONFIG_CODEC_ES8311_SUPPORT=1 \
    -I"$root/tests/board_audio/stubs" -I"$root/projects/echoy/main" \
    -I"$codec/include" -I"$codec/interface" -I"$codec/device/include" \
    "$root/tests/board_audio/test_board_audio.c" "$root/projects/echoy/main/board_audio.c" \
    -o "$board_build/test_board_audio"
"$board_build/test_board_audio"
