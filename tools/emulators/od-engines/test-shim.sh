#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
P=$(cd "$HERE/../../.." && pwd)
ROOT=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
SYS=$SDK/mipsel-buildroot-linux-musl/sysroot
CC=$SDK/bin/mipsel-buildroot-linux-musl-gcc
SRC=$P/package/lilsmoke-od-sdl
QA=$ROOT/engine-build/shim-qa
mkdir -p "$QA" "$ROOT/engine-logs"
exec > >(tee "$ROOT/engine-logs/shim-qa.log") 2>&1
flags=(-O1 -g -fPIC -Wall -Wextra -Werror -mfp32 -mabi=32 -march=mips32r2 \
    -I"$SRC/src" -I"$P/package/opendingux-emulators")
"$CC" "${flags[@]}" -DOD_SDL_QA -c "$SRC/src/frontend.c" -o "$QA/frontend.o"
"$CC" "${flags[@]}" -I"$SYS/usr/include/SDL2" -c "$SRC/src/video.c" -o "$QA/video.o"
"$CC" "${flags[@]}" -I"$SYS/usr/include/SDL2" -c "$SRC/src/audio.c" -o "$QA/audio.o"
"$CC" "${flags[@]}" -c "$SRC/tests/pcm-mock.c" -o "$QA/pcm.o"
"$CC" "${flags[@]}" -c "$SRC/tests/input-mock.c" -o "$QA/input.o"
for api in 1 2; do
    include=$SYS/usr/include/SDL
    [ "$api" = 1 ] || include=$SYS/usr/include/SDL2
    "$CC" "${flags[@]}" -I"$include" -DOD_SDL_API="$api" -c "$SRC/src/events.c" -o "$QA/events$api.o"
done
"$CC" -shared -Wl,-z,defs "$QA/"*.o -lpthread -ldl -o "$QA/libod-sdl-qa.so"
for api in 1 2; do
    include=$SYS/usr/include/SDL; lib=SDL
    if [ "$api" = 2 ]; then include=$SYS/usr/include/SDL2; lib=SDL2; fi
    "$CC" "${flags[@]}" -I"$include" -DOD_SDL_API="$api" "$SRC/tests/sdl-test.c" \
        -L"$QA" -lod-sdl-qa -l"$lib" -ldl -lpthread -o "$QA/sdl$api-test"
    for mode in generic native; do
        native=0;[ "$mode" = generic ] || native=1
        timeout 15 qemu-mipsel -cpu 74Kf -L "$SYS" \
            -E LD_LIBRARY_PATH="$QA:$ROOT/engine-build/sdl-qa:$SYS/usr/lib:$SYS/lib" \
            -E LILSMOKE_OD_NATIVE=1 -E LILSMOKE_OD_QA="$native" \
            -E SDL_AUDIODRIVER=dummy -E SDL_VIDEODRIVER=dummy "$QA/sdl$api-test" "$mode"
    done
done
