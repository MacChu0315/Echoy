#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
prep_build=$(mktemp -d /tmp/echoy-preparation.XXXXXX)
trap 'rm -rf "$prep_build"' EXIT HUP INT TERM
python3 "$root/tools/generate_triangle_tables.py" --cc "${CC:-cc}" --output-dir "$prep_build"
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -UNDEBUG ${AUDIO_TEST_CFLAGS:-} \
 -DECHOY_USE_PREBUILT_TRIANGLES=1 -I"$root/projects/echoy/main" -I"$prep_build" \
 "$root/tests/audio/test_triangle_tables.c" "$root/projects/echoy/main/echoy_music.c" \
 -lm -o "$prep_build/tables"
"$prep_build/tables"
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -UNDEBUG -fno-builtin ${AUDIO_TEST_CFLAGS:-} \
 -DECHOY_USE_PREBUILT_TRIANGLES=1 -I"$root/projects/echoy/main" -I"$prep_build" \
 "$root/tests/audio/test_preparation_reuse.c" "$root/projects/echoy/main/echoy_audio.c" \
 "$root/projects/echoy/main/echoy_music.c" -Wl,--wrap=exp2f -lm -o "$prep_build/reuse"
"$prep_build/reuse"
python3 "$root/tests/audio/check_preparation.py" --triangle-dir "$prep_build"
