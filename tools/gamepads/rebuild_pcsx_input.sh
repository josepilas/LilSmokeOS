#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
S=$P/package/lilsmoke-ui/src/src
R=/root/lilsmoke-psx-20261010/native
Q=/root/lilsmoke-hidapi-20261011
test -f "$R/libpcsx4all_rkgame.a"
sha256sum "$R/libpcsx4all_rkgame.a" > "$Q/pcsx-core-before.sha256"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O3 -mips32r2 -mplt -mno-shared \
    -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
    -I"$P/package/pcsx4all/src/src/port/rkgame" -I"$S" -I"$P/package/lilsmoke-scripts/src" \
    -I"$B/staging/usr/include/SDL2" \
    "$P/package/pcsx4all/src/pcsx4all-launch.cpp" "$P/package/pcsx4all/src/native-menu.cpp" \
    "$S/Input.cpp" "$S/Keymap.cpp" "$S/Log.cpp" "$R/libpcsx4all_rkgame.a" \
    -no-pie -Wl,-Ttext-segment=0x40000000 -lSDL2 -lSDL2_ttf -lpng -lz -lm -lpthread -lrt \
    -o "$Q/pcsx4all-launch"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" "$Q/pcsx4all-launch"
sha256sum -c "$Q/pcsx-core-before.sha256"
echo 'PCSX4ALL emulation core unchanged; input frontend rebuilt for HIDAPI shared state.'
