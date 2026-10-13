#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
producer_build=$(mktemp -d /tmp/echoy-art-producer.XXXXXX)
trap 'rm -rf "$producer_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -UNDEBUG ${ART_PRODUCER_CFLAGS:-} \
    -I"$root/projects/echoy/main" -I"$root/projects/echoy/managed_components/espressif__esp-gsp/include" \
    "$root/tests/art_producer/test_producer.c" "$root/projects/echoy/main/echoy_art_producer.c" \
    "$root/projects/echoy/main/echoy_art.c" -pthread -lm -Wl,--wrap=echoy_art_draw \
    -Wl,--wrap=pthread_create -Wl,--wrap=pthread_timedjoin_np -Wl,--wrap=free \
    -o "$producer_build/test-producer"
"$producer_build/test-producer"

"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -UNDEBUG ${ART_PRODUCER_CFLAGS:-} -DESP_PLATFORM \
    -I"$root/tests/art_producer/include" -I"$root/projects/echoy/main" \
    -I"$root/projects/echoy/managed_components/espressif__esp-gsp/include" \
    "$root/tests/art_producer/test_producer.c" "$root/tests/art_producer/device_fakes.c" \
    "$root/projects/echoy/main/echoy_art_producer.c" "$root/projects/echoy/main/echoy_art.c" \
    -pthread -lm -Wl,--wrap=echoy_art_draw -Wl,--wrap=pthread_create \
    -Wl,--wrap=pthread_timedjoin_np -Wl,--wrap=free -o "$producer_build/test-device-producer"
"$producer_build/test-device-producer"
