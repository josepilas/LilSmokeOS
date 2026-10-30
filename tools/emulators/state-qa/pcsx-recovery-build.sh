#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=${PCSX_QA_OUTPUT:-/root/pcsx-state-recovery-20261021}
R=/root/lilsmoke-psx-20261010/native
CXX=$B/host/bin/mipsel-buildroot-linux-musl-g++
S=$P/package/lilsmoke-ui/src/src
mkdir -p "$Q"
printf '%s  %s\n' '1dd1db27ebf46965cbd24b78880337fe60be571a3906e5187ade52090bc1359a' \
    "$R/libpcsx4all_rkgame.a" | sha256sum -c -
"$CXX" -std=c++17 -Os -I"$P/package/pcsx4all/src" \
    -I"$P/package/pcsx4all/src/src/port/rkgame" \
    "$P/tools/emulators/state-qa/pcsx-readonly-test.cpp" -lz -o "$Q/pcsx-readonly-test"
"$CXX" -std=c++17 -O2 -fno-delete-null-pointer-checks -mips32r2 -mplt -mno-shared -DPSXREC -DGPU_UNAI -DUSE_GPULIB -DSPU_PCSXREARMED \
    -I"$R/src" -I"$R/src/port/rkgame" -I"$R/src/plugin_lib" -I"$P/package/pcsx4all/src" \
    "$P/tools/emulators/state-qa/pcsx-recovery-engine.cpp" "$R/libpcsx4all_rkgame.a" \
    -no-pie -Wl,-Ttext-segment=0x40000000 -Wl,--wrap=malloc -lSDL2 -lpng -lz -lm -lpthread -lrt \
    -o "$Q/pcsx-recovery-engine"
"$CXX" -std=c++17 -O3 -fno-delete-null-pointer-checks -mips32r2 -mplt -mno-shared \
    -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
    -I"$P/package/pcsx4all/src/src/port/rkgame" -I"$S" -I"$P/package/lilsmoke-scripts/src" \
    -I"$B/staging/usr/include/SDL2" \
    "$P/package/pcsx4all/src/pcsx4all-launch.cpp" "$P/package/pcsx4all/src/native-menu.cpp" \
    "$S/Input.cpp" "$S/Keymap.cpp" "$S/Log.cpp" "$R/libpcsx4all_rkgame.a" \
    -no-pie -Wl,-Ttext-segment=0x40000000 -lSDL2 -lSDL2_ttf -lpng -lz -lm -lpthread -lrt \
    -o "$Q/pcsx4all-launch"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" "$Q/pcsx4all-launch"
"$CXX" -std=c++17 -O2 -fno-delete-null-pointer-checks -mips32r2 -mplt -mno-shared \
    -DPSXREC -DGPU_UNAI -DUSE_GPULIB -DSPU_PCSXREARMED \
    -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
    -I"$R/src" -I"$R/src/port/rkgame" -I"$R/src/plugin_lib" -I"$S" \
    -I"$P/package/lilsmoke-scripts/src" -I"$B/staging/usr/include/SDL2" \
    "$P/tools/emulators/state-qa/pcsx-recovery-menu.cpp" \
    "$S/Input.cpp" "$S/Keymap.cpp" "$S/Log.cpp" "$R/libpcsx4all_rkgame.a" \
    -no-pie -Wl,-Ttext-segment=0x40000000 -Wl,--wrap=SDL_Delay,--wrap=pcsx_load_game \
    -lSDL2 -lSDL2_ttf -lpng -lz -lm -lpthread -lrt -o "$Q/pcsx-recovery-menu"
sha256sum "$R/libpcsx4all_rkgame.a" "$Q/pcsx4all-launch" "$Q/pcsx-recovery-engine" > "$Q/pcsx-recovery-build.sha256"
