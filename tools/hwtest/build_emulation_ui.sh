#!/bin/bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build=${1:-/root/lilsmoke-os/build/buildroot/output}
out=/root/lilsmoke-performance/ui-build
mkdir -p "$out/src"
cp "$root/package/lilsmoke-ui/src/Makefile" "$out/Makefile"
cp "$root/package/lilsmoke-ui/src/src/"*.{cpp,h} "$out/src/"
sysroot="$build/host/mipsel-buildroot-linux-musl/sysroot"
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot/usr/lib/pkgconfig:$sysroot/usr/share/pkgconfig"
pkg="$build/host/bin/pkg-config"
flags="-std=c++17 -O3 -flto -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections $($pkg --cflags sdl2 SDL2_image SDL2_ttf libpng)"
make -C "$out" -j2 CXX="$build/host/bin/mipsel-buildroot-linux-musl-g++" \
    CXXFLAGS="$flags" LDFLAGS="-flto -Wl,--gc-sections" \
    LDLIBS="$($pkg --libs sdl2 SDL2_image SDL2_ttf libpng) -lz"
"$build/host/bin/mipsel-buildroot-linux-musl-strip" "$out/lilsmoke-ui"
cp "$out/lilsmoke-ui" "$root/test-results/emulation-performance/lilsmoke-ui-input-fix"
echo EMULATION_UI_MIPS_BUILT
