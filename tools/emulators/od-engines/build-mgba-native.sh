#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
R=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
export OD_SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host} OD_SYSROOT="$R/engine-sysroot"
patch=$P/package/lilsmoke-od-engines/patches/mgba-native.patch
if ! grep -q 'od_resolve_content(chosen' "$R/sources/mgba-native/src/platform/sdl/main.c" && \
   ! git -C "$R/sources/mgba-native" apply --reverse --check --recount "$patch" 2>/dev/null; then
    git -C "$R/sources/mgba-native" apply --check --recount "$patch"
    git -C "$R/sources/mgba-native" apply --recount "$patch"
fi
for name in mgba-framebuffer mgba-pause-header mgba-trace; do
    patch=$P/package/lilsmoke-od-engines/patches/$name.patch
    if ! git -C "$R/sources/mgba-native" apply --reverse --check --recount --unidiff-zero "$patch" 2>/dev/null; then
        git -C "$R/sources/mgba-native" apply --recount --unidiff-zero "$patch"
    fi
done
cmake -S "$R/sources/mgba-native" -B "$R/engine-build/mgba-native" \
    -DCMAKE_TOOLCHAIN_FILE="$P/tools/emulators/od-engines/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SDL=ON -DBUILD_QT=OFF -DBUILD_LIBRETRO=OFF \
    -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DBUILD_TEST=OFF \
    -DBUILD_PYTHON=OFF -DENABLE_SCRIPTING=OFF -DENABLE_DEBUGGERS=OFF -DENABLE_GDB_STUB=OFF \
    -DUSE_FFMPEG=OFF -DUSE_LIBZIP=OFF -DUSE_LIBZ=ON -DUSE_SQLITE3=OFF -DUSE_ELF=OFF \
    -DUSE_DISCORD_RPC=OFF -DUSE_EPOXY=OFF \
    -DCMAKE_C_FLAGS="-O3 -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -I$P/package/lilsmoke-od-engines/src -I$P/package/lilsmoke-od-consoles -I$R/sources/prboom/prboom2/src/TEXTSCREEN" \
    -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG"
cmake --build "$R/engine-build/mgba-native" --target mgba-sdl -j1
mkdir -p "$R/built-native/bin"
install -m 0755 "$R/engine-build/mgba-native/sdl/mgba" "$R/built-native/bin/mgba"
"$OD_SDK/bin/mipsel-buildroot-linux-musl-strip" "$R/built-native/bin/mgba"
mkdir -p "$R/built-native/lib" "$R/built-native/licenses/mgba"
cp -L "$R/engine-build/mgba-native/libmgba.so.0.11" "$R/built-native/lib/libmgba.so.0.11"
"$OD_SDK/bin/mipsel-buildroot-linux-musl-strip" "$R/built-native/lib/libmgba.so.0.11"
cp "$R/sources/mgba-native/LICENSE" "$R/built-native/licenses/mgba/"
