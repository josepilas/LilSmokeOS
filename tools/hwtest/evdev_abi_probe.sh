#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build=${BUILDROOT_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
out=/root/lilsmoke-performance/evdev-abi
mkdir -p "$out"
for compiler in gcc g++; do
    "$build/host/bin/mipsel-buildroot-linux-musl-$compiler" "$root/tools/hwtest/evdev_abi_probe.c" -o "$out/$compiler"
    qemu-mipsel -cpu 74Kf -L "$build/host/mipsel-buildroot-linux-musl/sysroot" "$out/$compiler"
done
