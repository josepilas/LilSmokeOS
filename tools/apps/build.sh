#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=/root/lilsmoke-applications-20261009
mkdir -p "$Q"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$P/package/lilsmoke-scripts/src" \
    "$P/package/lilsmoke-apps/src/lilsmoke-apps.cpp" -larchive -ljson-c -lsodium -o "$Q/host-helper"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra -Werror \
    -I"$P/package/lilsmoke-scripts/src" "$P/package/lilsmoke-apps/src/lilsmoke-apps.cpp" \
    -larchive -ljson-c -lsodium -o "$Q/mips-helper"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" "$Q/mips-helper"
install -m 0755 "$Q/mips-helper" "$B/target/usr/lib/lilsmoke/lilsmoke-apps"
mkdir -p "$Q/input-adapter"
make -C "$Q/input-adapter" -f "$P/package/lilsmoke-od-sdl/Makefile" -j1 \
    CC="$B/host/bin/mipsel-buildroot-linux-musl-gcc" \
    CXX="$B/host/bin/mipsel-buildroot-linux-musl-g++" \
    CFLAGS=-O2 CXXFLAGS=-O2 PROJECT="$P" \
    SDL2_INCLUDE="$B/staging/usr/include/SDL2" SDL12_INCLUDE="$B/staging/usr/include/SDL"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" "$Q/input-adapter/libod-sdl.so"
install -m 0755 "$Q/input-adapter/libod-sdl.so" "$B/target/usr/lib/lilsmoke/libod-sdl.so"
