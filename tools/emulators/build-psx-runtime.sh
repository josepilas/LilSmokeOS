#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=/root/lilsmoke-psx-20261010
CROSS=$B/host/bin/mipsel-buildroot-linux-musl-
mkdir -p "$Q/native" "$Q/adapter" "$Q/logs"
# Reuse the SDK; do not regenerate or reconfigure Buildroot's shared toolchain.
flags='-O3 -g0 -mips32r2 -mtune=74kc -mfp32 -mhard-float -mplt -mno-shared -fno-delete-null-pointer-checks'
if [ "${1:-}" != --frontend-only ]; then
    cp -a "$P/package/pcsx4all/src/." "$Q/native/"
    make -C "$Q/native" -f Makefile.rkgame NO_SDL=1 clean >"$Q/logs/native.log" 2>&1
    make -C "$Q/native" -f Makefile.rkgame -j1 CROSS_COMPILE="$CROSS" \
        NO_SDL=1 SOUND_DRIVERS=sdl2 EXTRA_CFLAGS="$flags" >>"$Q/logs/native.log" 2>&1
else
    cp "$P/package/pcsx4all/src/pcsx4all-launch.cpp" "$Q/native/pcsx4all-launch.cpp"
    cp "$P/package/pcsx4all/src/native-menu.cpp" "$Q/native/native-menu.cpp"
fi
"${CROSS}g++" $flags -std=c++17 -fno-pie \
    -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
    -I"$Q/native/src/port/rkgame" -I"$P/package/lilsmoke-ui/src/src" \
    -I"$P/package/lilsmoke-scripts/src" \
    "$Q/native/pcsx4all-launch.cpp" "$Q/native/native-menu.cpp" \
    "$P/package/lilsmoke-ui/src/src/Input.cpp" "$P/package/lilsmoke-ui/src/src/Keymap.cpp" \
    "$P/package/lilsmoke-ui/src/src/Log.cpp" "$Q/native/libpcsx4all_rkgame.a" \
    -no-pie -Wl,-Ttext-segment=0x40000000 -lSDL2 -lSDL2_ttf -lpng -lz -lm -lpthread -lrt \
    -o "$Q/pcsx4all-launch"
"${CROSS}strip" "$Q/pcsx4all-launch"
make -C "$Q/adapter" -f "$P/package/lilsmoke-od-sdl/Makefile" -j1 \
    CC="${CROSS}gcc" CXX="${CROSS}g++" CFLAGS=-O2 CXXFLAGS=-O2 PROJECT="$P" \
    SDL2_INCLUDE="$B/staging/usr/include/SDL2" SDL12_INCLUDE="$B/staging/usr/include/SDL"
"${CROSS}strip" "$Q/adapter/libod-sdl.so"
"${CROSS}g++" -std=c++17 -O2 -I"$P/package/lilsmoke-scripts/src" \
    "$P/package/lilsmoke-scripts/src/lilsmoke-emulators.cpp" -o "$Q/lilsmoke-emulators"
"${CROSS}strip" "$Q/lilsmoke-emulators"
install -m 0755 "$Q/pcsx4all-launch" "$B/target/usr/bin/pcsx4all-launch"
install -m 0755 "$Q/adapter/libod-sdl.so" "$B/target/usr/lib/lilsmoke/libod-sdl.so"
install -m 0755 "$Q/lilsmoke-emulators" "$B/target/usr/lib/lilsmoke/lilsmoke-emulators"
for script in lilsmoke-launch lilsmoke-od-launch lilsmoke-retroarch; do
    sh -n "$P/package/lilsmoke-scripts/src/$script"
    install -m 0755 "$P/package/lilsmoke-scripts/src/$script" "$B/target/usr/lib/lilsmoke/$script"
done
sha256sum "$Q/pcsx4all-launch" "$Q/adapter/libod-sdl.so"
