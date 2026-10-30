#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
B=$(printenv OD_BUILDROOT_OUTPUT || printf /root/lilsmoke-os/build/buildroot/output)
OUT=$(printenv OD_EXPANSION || printf /root/lilsmoke-emulator-expansion-20261008)
mkdir -p "$OUT/adapter" "$OUT/stage/usr/lib/lilsmoke"
test "${OD_JOBS:-1}" = 1
make -C "$OUT/adapter" -f "$P/package/lilsmoke-od-sdl/Makefile" -j1 \
    PROJECT="$P" CC="$B/host/bin/mipsel-buildroot-linux-musl-gcc" \
    CXX="$B/host/bin/mipsel-buildroot-linux-musl-g++" CFLAGS="-Os -march=mips32r2 -mabi=32 -mtune=74kc" \
    CXXFLAGS="-Os -march=mips32r2 -mabi=32 -mtune=74kc" \
    SDL2_INCLUDE="$B/staging/usr/include/SDL2" SDL12_INCLUDE="$B/staging/usr/include/SDL"
cp "$OUT/adapter/libod-sdl.so" "$OUT/stage/usr/lib/lilsmoke/libod-sdl.so"
"$B/host/bin/mipsel-buildroot-linux-musl-readelf" -d "$OUT/adapter/libod-sdl.so"
