#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
SDK=/root/lilsmoke-os/build/buildroot/output
OUT=${LILSMOKE_BOOTSTRAP_OUT:-/root/lilsmoke-public-bootstrap}
mkdir -p "$OUT/objects"
compile() {
    "$SDK/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra \
        -mips32r2 -mtune=74kc -mfp32 -mhard-float -mdspr2 \
        -I"$SDK/staging/usr/include/SDL2" \
        -I"$ROOT/package/lilsmoke-sm64-original/src" \
        -I"$ROOT/package/lilsmoke-ui/src/src" \
        -I"$ROOT/package/portsmoke-sm64/src/vendor/tinygl/src" \
        -c "$1" -o "$2"
}
compile "$HERE/bootstrap.cpp" "$OUT/bootstrap.o"
compile "$ROOT/package/lilsmoke-sm64-original/src/input_bridge.cpp" "$OUT/objects/lilsmoke-input.o"
compile "$ROOT/package/lilsmoke-ui/src/src/Input.cpp" "$OUT/objects/lilsmoke-common-input.o"
compile "$ROOT/package/lilsmoke-ui/src/src/Keymap.cpp" "$OUT/objects/lilsmoke-keymap.o"
compile "$ROOT/package/lilsmoke-ui/src/src/Log.cpp" "$OUT/objects/lilsmoke-log.o"
"$SDK/host/bin/mipsel-buildroot-linux-musl-g++" "$OUT/bootstrap.o" "$OUT/objects/"*.o \
    -L"$SDK/staging/usr/lib" -lSDL2 -lwebp -ljson-c -lpthread -ldl \
    -o "$OUT/sm64-prepare"
"$SDK/host/bin/mipsel-buildroot-linux-musl-strip" "$OUT/sm64-prepare"
printf '%s\n' 'Built the asset-free extraction UI; no original graphics/game objects linked.'
