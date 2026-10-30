#!/bin/sh
set -eu
R=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
src=$R/sources/sdl-gfx12
pin=4cc9485e8b36fab6126ee390c5746bbe16dcd3e6
if [ ! -d "$src/.git" ]; then
    git clone --no-checkout https://github.com/ferzkopp/SDL_gfx.git "$src"
    git -C "$src" checkout --detach "$pin"
fi
test "$(git -C "$src" rev-parse HEAD)" = "$pin"
obj=$R/engine-build/sdl-gfx12
mkdir -p "$obj" "$R/built-native/lib" "$R/built-native/licenses/sdl-gfx"
for name in SDL_framerate SDL_rotozoom SDL_gfxPrimitives SDL_gfxBlitFunc; do
    "$SDK/bin/mipsel-buildroot-linux-musl-gcc" -Os -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -fPIC \
        -I"$SDK/mipsel-buildroot-linux-musl/sysroot/usr/include/SDL" \
        -c "$src/$name.c" -o "$obj/$name.o"
    cp "$src/$name.h" "$R/engine-sysroot/usr/include/SDL/"
done
"$SDK/bin/mipsel-buildroot-linux-musl-gcc" -shared -Wl,-z,defs -Wl,-soname,libSDL_gfx.so.15 \
    "$obj/"*.o -lSDL -lm -o "$R/built-native/lib/libSDL_gfx.so.15"
cp "$R/built-native/lib/libSDL_gfx.so.15" "$R/engine-sysroot/usr/lib/"
ln -sf libSDL_gfx.so.15 "$R/engine-sysroot/usr/lib/libSDL_gfx.so"
cp "$src/LICENSE" "$R/built-native/licenses/sdl-gfx/"
