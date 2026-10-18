#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build=$(mktemp -d /tmp/echoy-pickup-tests.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -O2 -g ${PICKUP_TEST_CFLAGS:-} -Iprojects/echoy/main \
 tests/pickup/test_pcm.c projects/echoy/main/echoy_music.c projects/echoy/main/echoy_model.c \
 projects/echoy/main/echoy_arrange.c projects/echoy/main/echoy_audio.c \
 projects/echoy/main/echoy_pickup.c projects/echoy/main/echoy_device_pcm.c -lm -o "$build/pcm"
"$build/pcm"
node tests/pickup/oracle.mjs

cc -std=c11 -Wall -Wextra -Werror -O2 -g ${PICKUP_TEST_CFLAGS:-} -Iprojects/echoy/main \
 -Iprojects/echoy/managed_components/espressif__esp-gsp/include tests/pickup/test_art.c -lm -o "$build/art"
"$build/art"

sh tests/pickup/run_local_reply.sh
