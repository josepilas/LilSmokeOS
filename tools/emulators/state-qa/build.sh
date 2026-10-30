#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=${1:?isolated QA directory required}
R=/root/lilsmoke-psx-20261010/native
mkdir -p "$Q"
printf '%s  %s\n' '1dd1db27ebf46965cbd24b78880337fe60be571a3906e5187ade52090bc1359a' \
    "$R/libpcsx4all_rkgame.a" | sha256sum -c -
sha256sum "$R/libpcsx4all_rkgame.a" > "$Q/core-before.sha256"
S=$P/package/lilsmoke-ui/src/src
CXX=$B/host/bin/mipsel-buildroot-linux-musl-g++
"$CXX" -std=c++17 -O2 -mips32r2 -mplt -mno-shared \
    -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
    -I"$P/package/pcsx4all/src/src/port/rkgame" -I"$S" -I"$P/package/lilsmoke-scripts/src" \
    -I"$B/staging/usr/include/SDL2" \
    "$P/package/pcsx4all/src/pcsx4all-launch.cpp" "$P/package/pcsx4all/src/native-menu.cpp" \
    "$S/Input.cpp" "$S/Keymap.cpp" "$S/Log.cpp" "$R/libpcsx4all_rkgame.a" \
    -no-pie -Wl,-Ttext-segment=0x40000000 -lSDL2 -lSDL2_ttf -lpng -lz -lm -lpthread -lrt \
    -o "$Q/pcsx4all-launch"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" "$Q/pcsx4all-launch"
"$CXX" -std=c++17 -O2 -mips32r2 -mplt -mno-shared -DPSXREC -DGPU_UNAI -DUSE_GPULIB -DSPU_PCSXREARMED \
    -I"$R/src" -I"$R/src/port/rkgame" -I"$R/src/plugin_lib" -I"$P/package/pcsx4all/src" \
    "$P/tools/emulators/state-qa/pcsx-roundtrip.cpp" "$R/libpcsx4all_rkgame.a" \
    -no-pie -Wl,-Ttext-segment=0x40000000 -lSDL2 -lpng -lz -lm -lpthread -lrt \
    -o "$Q/pcsx-roundtrip"
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -std=gnu11 -O2 \
    -I"$B/build/retroarch-mips-v1.19.1/libretro-common/include" \
    "$P/tools/emulators/state-qa/libretro-roundtrip.c" -ldl -o "$Q/libretro-roundtrip"
sha256sum -c "$Q/core-before.sha256"
