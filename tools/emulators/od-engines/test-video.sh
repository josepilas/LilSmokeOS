#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
R=/root/lilsmoke-emulator-expansion-20261008
Q=$R/engine-build/video-qa
mkdir -p "$Q"
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -O2 -Wall -Wextra -Werror \
    -I"$B/staging/usr/include/SDL2" -I"$P/package/lilsmoke-od-sdl/src" \
    "$P/package/lilsmoke-od-sdl/tests/video-test.c" -lSDL2 -ldl -Wl,-export-dynamic -o "$Q/test"
timeout 30 qemu-mipsel -cpu 74Kf -L "$B/staging" \
    -E LD_LIBRARY_PATH="$R/engine-build/sdl-qa:$B/staging/usr/lib:$B/staging/lib" \
    -E SDL_VIDEODRIVER=dummy "$Q/test"
