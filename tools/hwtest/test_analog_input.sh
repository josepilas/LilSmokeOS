#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=/root/lilsmoke-analog-input-20261011
S=$P/package/lilsmoke-ui/src/src
mkdir -p "$Q"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$S" "$P/tools/hwtest/test_analog_input.cpp" "$S/Input.cpp" "$S/Keymap.cpp" "$S/Log.cpp" \
    $(pkg-config --cflags --libs sdl2) -o "$Q/host"
"$Q/host"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra -Werror \
    -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
    -I"$S" -I"$B/staging/usr/include/SDL2" "$P/tools/hwtest/test_analog_input.cpp" \
    "$S/Input.cpp" "$S/Keymap.cpp" "$S/Log.cpp" -lSDL2 -o "$Q/mips"
qemu-mipsel -cpu 74Kf -L "$B/staging" "$Q/mips"
