#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PROJECT=$(cd "$HERE/../../.." && pwd)
ROOT=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
SYS=$SDK/mipsel-buildroot-linux-musl/sysroot
QA=$ROOT/engine-build/production-qa
SRC=$PROJECT/package/lilsmoke-od-sdl
mkdir -p "$QA" "$ROOT/engine-logs"
exec > >(tee -a "$ROOT/engine-logs/production-qa.log") 2>&1
LIB=$ROOT/built-native/libod-sdl.so
test -s "$LIB"
if "$SDK/bin/mipsel-buildroot-linux-musl-nm" -D "$LIB" | grep -q qa_pcm; then
    echo 'Refusing production library containing mock PCM symbols' >&2
    exit 1
fi
for api in 1 2; do
    include=$SYS/usr/include/SDL; sdl=SDL
    if [ "$api" = 2 ]; then include=$SYS/usr/include/SDL2; sdl=SDL2; fi
    "$SDK/bin/mipsel-buildroot-linux-musl-gcc" -O1 -Wall -Wextra -Werror -mabi=32 -mfp32 \
        -I"$SRC/src" -I"$include" "$SRC/tests/production-generic.c" \
        -L"$ROOT/built-native" -lod-sdl -l"$sdl" -o "$QA/sdl$api-production"
    for hcfb in 0 1; do
        timeout 20 qemu-mipsel -cpu 74Kf -L "$SYS" \
            -E LD_LIBRARY_PATH="$ROOT/built-native:$SYS/usr/lib:$SYS/lib" \
            -E LILSMOKE_OD_NATIVE=1 -E LILSMOKE_HCFB="$hcfb" -E LILSMOKE_OD_QA=1 \
            -E SDL_AUDIODRIVER=dummy "$QA/sdl$api-production"
    done
done
sha256sum "$LIB"
echo 'Production binary tested without mock PCM; generic audio uses upstream dummy. Not hardware acceptance.'
