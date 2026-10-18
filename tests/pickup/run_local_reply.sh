#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build=$(mktemp -d /tmp/echoy-pickup-local-reply.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc -std=c11 -O1 -g -Wall -Wextra -Werror ${PICKUP_TEST_CFLAGS:-} -Iprojects/echoy/main \
 tests/pickup/test_local_reply.c projects/echoy/main/echoy_group.c \
 projects/echoy/main/echoy_link_protocol.c projects/echoy/main/echoy_link_codec.c \
 projects/echoy/main/echoy_band.c projects/echoy/main/echoy_arrange.c \
 projects/echoy/main/echoy_model.c projects/echoy/main/echoy_music.c \
 projects/echoy/main/echoy_audio.c projects/echoy/main/echoy_pickup.c -lm -o "$build/check"
"$build/check"
