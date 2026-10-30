#!/bin/sh
set -eu
build=${BUILDROOT_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
out=/root/lilsmoke-performance/kernel
mkdir -p "$out"
if [ ! -f "$out/vmlinux-before" ]; then cp "$build/images/vmlinux" "$out/vmlinux-before"; fi
kernel="$build/build/linux-5.15.148"
"$kernel/scripts/config" --file "$kernel/.config" --set-str UEVENT_HELPER_PATH ''
make -C "$kernel" ARCH=mips CROSS_COMPILE="$build/host/bin/mipsel-buildroot-linux-musl-" -j4 vmlinux >"$out/build.log" 2>&1
cp "$kernel/vmlinux" "$build/images/vmlinux"
sha256sum "$out/vmlinux-before" "$build/images/vmlinux"
