#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
fixed_build=$(mktemp -d /tmp/echoy-fixed-audio.XXXXXX)
trap 'rm -rf "$fixed_build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -g -UNDEBUG ${AUDIO_TEST_CFLAGS:-} \
 -I"$root/projects/echoy/main" "$root/tests/audio/test_fixed_audio.c" \
 "$root/projects/echoy/main/echoy_fixed_audio.c" "$root/projects/echoy/main/echoy_audio.c" \
 "$root/projects/echoy/main/echoy_music.c" "$root/projects/echoy/main/echoy_device_pcm.c" \
 -Wl,--wrap=calloc -lm -o "$fixed_build/test"
for profile in office device reference; do
 python3 "$root/tools/generate_fixed_audio.py" --cc "${CC:-cc}" --profile "$profile" --output "$fixed_build/$profile.bin"
 "$fixed_build/test" "$fixed_build/$profile.bin" "$profile"
done
export ECHOY_TEST_OFFICE="$fixed_build/office.bin" ECHOY_TEST_REFERENCE="$fixed_build/reference.bin"
for test in test_fixed_band test_random_preview; do
 test_source="$root/tests/audio/test_fixed_band.c"
 wraps="-Wl,--wrap=calloc -Wl,--wrap=echoy_audio_render_cue -Wl,--wrap=echoy_audio_render_preview_pool"
 if [ "$test" = test_random_preview ]; then
  test_source="$root/tests/band/test_random_preview.c"
  wraps=""
 fi
 "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -g -UNDEBUG -DECHOY_USE_FIXED_AUDIO=1 ${AUDIO_TEST_CFLAGS:-} \
  -I"$root/projects/echoy/main" "$test_source" "$root/tests/audio/fixed_audio_provider.c" \
  "$root/projects/echoy/main/echoy_fixed_audio.c" "$root/projects/echoy/main/echoy_band.c" \
  "$root/projects/echoy/main/echoy_model.c" "$root/projects/echoy/main/echoy_music.c" \
  "$root/projects/echoy/main/echoy_arrange.c" "$root/projects/echoy/main/echoy_audio.c" \
  "$root/projects/echoy/main/echoy_pickup.c" "$root/projects/echoy/main/echoy_device_pcm.c" \
  $wraps -lm -o "$fixed_build/$test"
 "$fixed_build/$test"
done
