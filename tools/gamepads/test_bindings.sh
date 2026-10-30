#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=/root/lilsmoke-hidapi-20261011
S=$P/package/lilsmoke-ui/src/src
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$S" \
    "$P/tools/gamepads/test_sm64_bindings.cpp" "$S/Input.cpp" "$S/Keymap.cpp" "$S/Log.cpp" \
    $(pkg-config --cflags --libs sdl2) -o "$Q/bindings-host"
"$Q/bindings-host"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra -Werror -I"$S" \
    -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" -I"$B/staging/usr/include/SDL2" \
    "$P/tools/gamepads/test_sm64_bindings.cpp" "$S/Input.cpp" "$S/Keymap.cpp" "$S/Log.cpp" -lSDL2 -o "$Q/bindings-mips"
qemu-mipsel -cpu 74Kf -L "$B/staging" "$Q/bindings-mips"
