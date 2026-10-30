#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build=${BUILDROOT_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
out=/root/lilsmoke-performance/video-smoke
mkdir -p "$out"
cp "$root/test-results/emulation-performance/libSDL2-candidate.so" "$out/libSDL2-2.0.so.0"
"$build/host/bin/mipsel-buildroot-linux-musl-gcc" -O2 -mips32r2 -mfp32 -mhard-float \
    -I"$build/host/mipsel-buildroot-linux-musl/sysroot/usr/include/SDL2" \
    "$root/tools/hwtest/software_video_smoke.c" -lSDL2 -o "$out/video-smoke"
exec qemu-mipsel -cpu 74Kf -L "$build/host/mipsel-buildroot-linux-musl/sysroot" \
    -E LD_LIBRARY_PATH="$out" "$out/video-smoke"
