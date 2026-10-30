#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
ulimit -c 0
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=${1:-/root/lilsmoke-os/build/buildroot/output}
Q=${2:-/root/lilsmoke-directfb-dispatch-20261021-qualified}
mkdir -p "$Q/before" "$Q/after"
cp "$Q/baseline-libfusion.so" "$Q/before/libfusion-1.7.so.7"
cp "$Q/payload/libfusion-1.7.so.7.0.0" "$Q/after/libfusion-1.7.so.7"
cp "$Q/baseline-libdirect.so" "$Q/before/libdirect-1.7.so.7"
cp "$Q/payload/libdirect-1.7.so.7.0.0" "$Q/after/libdirect-1.7.so.7"
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$Q/source/lib" -I"$B/staging/usr/include/directfb" \
    "$P/tools/m8/test_fusion_dispatch.c" -lfusion -ldirect -lpthread \
    -o "$Q/test-fusion-dispatch"
printf 'phase\tmode\texit\n' > "$Q/results.tsv"
for phase in before after; do
    for mode in busy idle wake queue; do
        rc=0
        timeout 30 qemu-mipsel -cpu 74Kf -L "$B/target" \
            -E "LD_LIBRARY_PATH=$Q/$phase:$B/target/usr/lib" \
            "$Q/test-fusion-dispatch" "$mode" > "$Q/$phase-$mode.log" 2>&1 || rc=$?
        printf '%s %s exit=%s\n' "$phase" "$mode" "$rc"
        printf '%s\t%s\t%s\n' "$phase" "$mode" "$rc" >> "$Q/results.tsv"
        if [ "$phase" = after ]; then
            [ "$rc" = 0 ]
            grep -q "PASS Fusion dispatcher $mode" "$Q/$phase-$mode.log"
        elif [ "$mode" = busy ]; then
            [ "$rc" = 9 ]
            grep -q 'world was freed before its active callback finished' "$Q/$phase-$mode.log"
        fi
    done
done
echo 'PASS real MIPS Fusion user-mode regressions; no MMIO or physical FPS qualification.'
