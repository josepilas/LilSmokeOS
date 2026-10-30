#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=${1:-/root/lilsmoke-os/build/buildroot/output}
Q=${2:-/root/lilsmoke-directfb-dispatch-20261021-qualified}
source=$B/build/directfb-1.7.7
patch=$P/patches/directfb/0005-join-single-process-dispatcher-before-world-destruction.patch
thread_patch=$P/patches/directfb/0006-pop-thread-cleanup-on-early-join.patch
[ -f "$source/lib/fusion/fusion.c" ] && [ -f "$patch" ]
[ ! -e "$Q/source" ] || { echo 'Refusing to reuse a mixed DirectFB source build' >&2; exit 1; }
mkdir -p "$Q/payload"
cp -a "$source" "$Q/source"
cp "$B/target/usr/lib/libfusion-1.7.so.7.0.0" "$Q/baseline-libfusion.so"
cp "$B/target/usr/lib/libdirect-1.7.so.7.0.0" "$Q/baseline-libdirect.so"
patch --dry-run -d "$Q/source" -p1 < "$patch"
patch -d "$Q/source" -p1 < "$patch"
patch --dry-run -d "$Q/source" -p1 < "$thread_patch"
patch -d "$Q/source" -p1 < "$thread_patch"
make -C "$Q/source/lib/direct" -j2 > "$Q/direct-build.log" 2>&1
make -C "$Q/source/lib/fusion" -j2 > "$Q/build.log" 2>&1
"$B/host/bin/mipsel-buildroot-linux-musl-strip" \
    -o "$Q/payload/libfusion-1.7.so.7.0.0" \
    "$Q/source/lib/fusion/.libs/libfusion-1.7.so.7.0.0"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" \
    -o "$Q/payload/libdirect-1.7.so.7.0.0" \
    "$Q/source/lib/direct/.libs/libdirect-1.7.so.7.0.0"
sha256sum "$patch" "$thread_patch" "$source/lib/fusion/fusion.c" "$Q/source/lib/fusion/fusion.c" \
    "$source/lib/direct/os/linux/glibc/thread.c" "$Q/source/lib/direct/os/linux/glibc/thread.c" \
    "$Q/baseline-libfusion.so" "$Q/baseline-libdirect.so" \
    "$Q/payload/libfusion-1.7.so.7.0.0" "$Q/payload/libdirect-1.7.so.7.0.0" > "$Q/inputs.sha256"
"$B/host/bin/mipsel-buildroot-linux-musl-readelf" -h -A \
    "$Q/payload/libfusion-1.7.so.7.0.0" > "$Q/elf.txt"
echo "Isolated DirectFB dispatcher build ready: $Q"
echo 'SDK target and physical card unchanged.'
