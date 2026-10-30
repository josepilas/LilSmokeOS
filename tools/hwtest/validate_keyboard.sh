#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD=${BUILD:-/root/lilsmoke-os/build/buildroot/output}
OUT=${OUT:-/tmp/lilsmoke-keyboard-tests}
mkdir -p "$OUT"
UI="$ROOT/package/lilsmoke-ui/src/src"

# The widget and its real Explorer consumer are linked together.
c++ -std=c++17 -O2 -I"$UI" \
    "$ROOT/tools/hwtest/virtual_keyboard_smoke.cpp" \
    "$UI/VirtualKeyboard.cpp" "$UI/FileExplorerView.cpp" \
    "$UI/BluetoothView.cpp" "$UI/Config.cpp" \
    "$UI/Renderer.cpp" "$UI/Font.cpp" "$UI/Log.cpp" \
    $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -lz \
    -o "$OUT/keyboard-host"
SDL_VIDEODRIVER=dummy LILSMOKE_UI_WINDOWED=1 \
    "$OUT/keyboard-host" "$OUT/images"

SYSROOT="$BUILD/host/mipsel-buildroot-linux-musl/sysroot"
"$BUILD/host/bin/mipsel-buildroot-linux-musl-g++" \
    -std=c++17 -O2 -I"$UI" -I"$SYSROOT/usr/include/SDL2" \
    "$ROOT/tools/hwtest/virtual_keyboard_smoke.cpp" \
    "$UI/VirtualKeyboard.cpp" "$UI/FileExplorerView.cpp" \
    "$UI/BluetoothView.cpp" "$UI/Config.cpp" \
    "$UI/Renderer.cpp" "$UI/Font.cpp" "$UI/Log.cpp" \
    -lSDL2_image -lSDL2_ttf -lSDL2 -lz -o "$OUT/keyboard-mips"
qemu-mipsel -cpu 74Kf -L "$SYSROOT" "$OUT/keyboard-mips"

cc -std=c99 -Wall -Wextra -Werror \
    "$ROOT/tools/hwtest/keyboard_api_fake.c" -o "$OUT/keyboard-api-fake"
cc -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -I"$ROOT/package/lilsmoke-ui/src/include" \
    "-DLILSMOKE_KEYBOARD_PROGRAM=\"$OUT/keyboard-api-fake\"" \
    "$ROOT/tools/hwtest/keyboard_api_smoke.c" -o "$OUT/keyboard-api"
"$OUT/keyboard-api"
printf '%s\n' KEYBOARD_VALIDATION_OK
