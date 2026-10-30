#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
R=/root/lilsmoke-psx-20261010/native
Q=${1:?isolated QA directory required}
S=$P/package/lilsmoke-ui/src/src
"$B/host/bin/mipsel-linux-g++" -std=c++17 -O2 -mips32r2 -mplt -mno-shared \
    -DPSXREC -DGPU_UNAI -DUSE_GPULIB -DSPU_PCSXREARMED \
    -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
    -I"$R/src" -I"$R/src/port/rkgame" -I"$R/src/plugin_lib" \
    -I"$P/package/pcsx4all/src/src/port/rkgame" -I"$S" -I"$P/package/lilsmoke-scripts/src" \
    -I"$B/staging/usr/include/SDL2" \
    "$P/tools/emulators/state-qa/pcsx-menu-ui.cpp" "$S/Input.cpp" "$S/Keymap.cpp" "$S/Log.cpp" \
    "$R/libpcsx4all_rkgame.a" -no-pie -Wl,-Ttext-segment=0x40000000 \
    -Wl,--wrap=SDL_Delay,--wrap=SDL_Quit,--wrap=pcsx_audio_pause,--wrap=execv,--wrap=access \
    -lSDL2 -lSDL2_ttf -lpng -lz -lm -lpthread -lrt -o "$Q/pcsx-menu-ui"
