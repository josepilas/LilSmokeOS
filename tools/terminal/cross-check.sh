#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Read-only SDK use. All generated files stay in the isolated output directory.
# shellcheck disable=SC2086
set -eu
P=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
B=${LILSMOKE_TERMINAL_TEST_OUT:-/tmp/lilsmoke-terminal-test}
CC="$SDK/host/bin/mipsel-linux-gcc"
CXX="$SDK/host/bin/mipsel-linux-g++"
mkdir -p "$B/mips"
test -f "$B/libvterm-0.3.3/src/vterm.c"
for src in "$B/libvterm-0.3.3"/src/*.c; do
    "$CC" -std=c99 -Os -Wall -I"$B/libvterm-0.3.3/include" \
        -c "$src" -o "$B/mips/$(basename "$src" .c).o"
done
"$SDK/host/bin/mipsel-linux-ar" rcs "$B/mips/libvterm.a" "$B/mips"/*.o
export PKG_CONFIG_SYSROOT_DIR="$SDK/staging"
export PKG_CONFIG_LIBDIR="$SDK/staging/usr/lib/pkgconfig:$SDK/staging/usr/share/pkgconfig"
FLAGS=$(pkg-config --cflags sdl2 SDL2_ttf)
LIBS=$(pkg-config --libs sdl2 SDL2_ttf)
for src in Terminal Pty Keyboard main; do
    # pkg-config returns compiler arguments, intentionally word-split here.
    "$CXX" -std=c++17 -Os -Wall -Wextra -Werror $FLAGS \
        -I"$B/libvterm-0.3.3/include" -c "$P/package/lilsmoke-terminal/src/$src.cpp" \
        -o "$B/mips/terminal-$src.o"
done
"$CXX" "$B/mips"/terminal-*.o "$B/mips/libvterm.a" $LIBS -o "$B/mips/lilsmoke-terminal"
"$SDK/host/bin/mipsel-linux-readelf" -h "$B/mips/lilsmoke-terminal"
# Verify just the changed Settings translation unit, never the complete UI.
"$CXX" -std=c++17 -fsyntax-only $FLAGS \
    -I"$P/package/lilsmoke-scripts/src" -I"$P/package/lilsmoke-media/src" \
    "$P/package/lilsmoke-ui/src/src/SettingsView.cpp"
sh -n "$P/package/lilsmoke-terminal/src/lilsmoke-terminal-launch"
# Exercise the shared-library package adapter without touching SDK staging or
# target. A host-configured libtool would silently select the host linker.
mkdir -p "$B/recipe"
tar -xzf "$B/libvterm-0.3.3.tar.gz" --strip-components=1 -C "$B/recipe"
cp "$P/package/libvterm/CMakeLists.txt" "$B/recipe/CMakeLists.txt"
cmake -S "$B/recipe" -B "$B/recipe-build" \
    -DCMAKE_TOOLCHAIN_FILE="$SDK/host/share/buildroot/toolchainfile.cmake" \
    -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build "$B/recipe-build" --parallel 1
DESTDIR="$B/recipe-stage" cmake --install "$B/recipe-build"
test -f "$B/recipe-stage/usr/include/vterm.h"
test -f "$B/recipe-stage/usr/lib/pkgconfig/vterm.pc"
test -f "$B/recipe-stage/usr/lib/libvterm.so.0"
"$SDK/host/bin/mipsel-linux-readelf" -d "$B/recipe-stage/usr/lib/libvterm.so.0" | grep -E 'SONAME|NEEDED'
"$CXX" "$B/mips"/terminal-*.o -L"$B/recipe-stage/usr/lib" -lvterm $LIBS \
    -o "$B/mips/lilsmoke-terminal-shared"
qemu-mipsel -cpu 74Kf -L "$SDK/staging" \
    -E "LD_LIBRARY_PATH=$B/recipe-stage/usr/lib:$SDK/staging/usr/lib:$SDK/staging/lib" \
    "$B/mips/lilsmoke-terminal-shared" --check-pty
echo 'PASS MIPS helper, Settings syntax, shared libvterm recipe and isolated staging layout'
