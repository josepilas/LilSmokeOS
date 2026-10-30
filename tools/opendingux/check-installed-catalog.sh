#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=$(printenv OD_BUILD || printf /root/lilsmoke-opendingux-qa)
B=$(printenv OD_BUILDROOT_OUTPUT || printf /root/lilsmoke-os/build/buildroot/output)
mkdir -p "$OUT"
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -Os -fPIC -shared \
    "$P/tools/opendingux/rlimit-qa-shim.c" -o "$OUT/rlimit-qa-shim.so"
# Read-only actual installed target tree. The shim bypasses only QEMU's
# inappropriate host-address-space limit; all catalog/ELF policies are production.
qemu-mipsel -cpu 74Kf -L "$B/target" -E LD_PRELOAD="$OUT/rlimit-qa-shim.so" \
    "$B/target/usr/lib/lilsmoke/lilsmoke-opendingux" scan
