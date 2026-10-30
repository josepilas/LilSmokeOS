#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
build=${BUILDROOT_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
out=/root/lilsmoke-performance/stretch-smoke
mkdir -p "$out"
"$build/host/bin/mipsel-buildroot-linux-musl-gcc" -O2 -mips32r2 -mfp32 -mhard-float \
    -I"$build/host/mipsel-buildroot-linux-musl/sysroot/usr/include/SDL2" \
    "$root/tools/hwtest/stretch_video_smoke.c" -lSDL2 -o "$out/stretch-smoke"
for variant in baseline-safe candidate; do
    mkdir -p "$out/$variant"
    cp "$root/test-results/emulation-performance/libSDL2-$variant.so" "$out/$variant/libSDL2-2.0.so.0"
    qemu-mipsel -cpu 74Kf -L "$build/host/mipsel-buildroot-linux-musl/sysroot" \
        -E LD_LIBRARY_PATH="$out/$variant" "$out/stretch-smoke" >"$out/$variant.log"
    if [ "${1:-}" = --benchmark ]; then
        qemu-mipsel -cpu 74Kf -L "$build/host/mipsel-buildroot-linux-musl/sysroot" \
            -E LD_LIBRARY_PATH="$out/$variant" "$out/stretch-smoke" --benchmark >"$out/$variant-bench.log"
        cat "$out/$variant-bench.log"
    fi
done
cmp "$out/baseline-safe.log" "$out/candidate.log"
echo STRETCH_BASELINE_EQUIVALENCE_OK_cases_36
