#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
export OD_SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
export OD_SYSROOT=$ROOT/engine-sysroot
SOURCE=$(dirname "$OD_SDK")/build/sdl2-2.28.5
BUILD=$ROOT/engine-build/sdl-qa
mkdir -p "$BUILD" "$ROOT/engine-logs"
test "${OD_JOBS:-1}" = 1
exec 9>"$ROOT/.native-build.lock"
flock -n 9 || { echo 'Another native heavy phase is active.' >&2; exit 2; }
exec > >(tee "$ROOT/engine-logs/sdl-qa-build.log") 2>&1
test -f "$SOURCE/CMakeLists.txt"
cmake -S "$SOURCE" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$HERE/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_C_FLAGS='-Os -march=mips32r2 -mabi=32 -mfp32' \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_TESTS=OFF \
    -DSDL_DUMMYVIDEO=ON -DSDL_DUMMYAUDIO=ON -DSDL_OFFSCREEN=OFF \
    -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_DIRECTFB=OFF -DSDL_KMSDRM=OFF \
    -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_ALSA=OFF \
    -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
    -DSDL_HIDAPI=OFF -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF
cmake --build "$BUILD" -j1
echo 'QA-only SDL2 with dummy video. Never stage this library for hardware.'
