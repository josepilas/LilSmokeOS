#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=$P/package/lilsmoke-ui/src/src
B=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
Q=${QA_OUTPUT:-$(mktemp -d /tmp/lilsmoke-game-navigation-XXXXXX)}
mkdir -p "$Q"
FONT=${QA_FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf}
set -- "$P/tools/ui/game_list_navigation_test.cpp" "$S/ListView.cpp" "$S/GridView.cpp" \
    "$S/Input.cpp" "$S/Keymap.cpp" "$S/VirtualKeyboard.cpp" "$S/Renderer.cpp" \
    "$S/Font.cpp" "$S/Theme.cpp" "$S/ThemeRender.cpp" "$S/Log.cpp"
echo "Isolated QA output: $Q"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$S" -I"$P/tools/gamepads" -I"$P/package/lilsmoke-usb-gamepads/src" \
    "$@" $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) \
    -lwebp -ldl -o "$Q/host"
ASAN_OPTIONS=detect_leaks=1 "$Q/host" "$P/board/qemu-mipsel/theme.xml" "$FONT"
QA_SMALL_SCREEN=1 ASAN_OPTIONS=detect_leaks=1 "$Q/host" "$P/board/qemu-mipsel/theme.xml" "$FONT"
if [ "${QA_MIPS:-1}" = 1 ]; then
    "$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra -Werror \
        -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
        -I"$S" -I"$P/tools/gamepads" -I"$P/package/lilsmoke-usb-gamepads/src" \
        -I"$B/staging/usr/include" "$@" \
        -lSDL2_image -lSDL2_ttf -lSDL2 -lwebp -ldl -o "$Q/mips"
    QA_HEADLESS=1 qemu-mipsel -cpu 74Kf -L "$B/staging" "$Q/mips" "$P/board/qemu-mipsel/theme.xml" "$FONT"
fi
