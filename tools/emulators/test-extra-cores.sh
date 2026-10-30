#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
ROOT=/root/lilsmoke-emulator-expansion-20261008
QA=$ROOT/core-qa
mkdir -p "$QA" "$P/test-results/emulator-expansion-2026-10-08"
CC=$B/host/bin/mipsel-buildroot-linux-musl-gcc
include=$ROOT/libretro/picodrive/platform/libretro/libretro-common/include
"$CC" -std=c99 -O2 -mips32r2 -I"$include" "$P/tools/hwtest/core_api_smoke.c" -ldl -o "$QA/core-api"
"$CC" -std=gnu99 -O2 -mips32r2 -I"$include" "$P/tools/hwtest/core_benchmark.c" -ldl -o "$QA/core-benchmark"
report=$P/test-results/emulator-expansion-2026-10-08/core-api.txt
: > "$report"
failed=0
for core in "$ROOT/built-libretro/"*_libretro.so; do
    printf '%s: ' "$(basename "$core")" >> "$report"
    timeout 30 qemu-mipsel -cpu 74Kf -L "$B/staging" \
        -E LD_LIBRARY_PATH="$ROOT/engine-sysroot/usr/lib:$ROOT/built-native/lib" \
        "$QA/core-api" "$core" >> "$report" 2>&1 || failed=1
done
cat "$report"
exit "$failed"
