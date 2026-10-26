#!/bin/sh
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/echoy-display-io.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc -std=c11 -O1 -Wall -Wextra -Werror -I"$root/tests/display_io/stubs" \
    "$root/tests/display_io/test_io.c" "$root/tests/display_io/fake_io.c" \
    "$root/projects/echoy/main/board_display_io.c" \
    -Wl,--wrap=esp_lcd_new_panel_io_spi -o "$build/check"
"$build/check"
# Match the firmware's archive order: the application archive precedes the
# BSP which introduces the reference to the wrapped SDK constructor.
for source in board_display_io fake_bsp fake_io; do
    case "$source" in
        board_display_io) file="$root/projects/echoy/main/$source.c" ;;
        *) file="$root/tests/display_io/$source.c" ;;
    esac
    cc -std=c11 -O1 -Wall -Wextra -Werror -I"$root/tests/display_io/stubs" \
        -c "$file" -o "$build/$source.o"
    ar rcs "$build/lib$source.a" "$build/$source.o"
done
cc -std=c11 -O1 -Wall -Wextra -Werror -I"$root/tests/display_io/stubs" \
    "$root/tests/display_io/test_link.c" "$build/libboard_display_io.a" \
    "$build/libfake_bsp.a" "$build/libfake_io.a" \
    -Wl,--wrap=esp_lcd_new_panel_io_spi -Wl,-u,__wrap_esp_lcd_new_panel_io_spi \
    -o "$build/link-check"
"$build/link-check"
