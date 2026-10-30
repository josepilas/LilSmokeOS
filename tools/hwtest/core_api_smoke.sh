#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build=${BUILDROOT_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
out=/root/lilsmoke-performance/core-api
mkdir -p "$out"
"$build/host/bin/mipsel-buildroot-linux-musl-gcc" -O2 \
    -I"$root/package/retroarch-mips/cores/snes9x2005/libretro-common/include" \
    "$root/tools/hwtest/core_api_smoke.c" -ldl -o "$out/core-api"
while IFS='|' read -r name url ref mk built installed; do
    case "$name" in ''|'#'*) continue ;; esac
    core="$root/test-results/emulation-performance/pinned-cores/$installed"
    echo "CHECK $name"
    qemu-mipsel -cpu 74Kf -L "$build/host/mipsel-buildroot-linux-musl/sysroot" "$out/core-api" "$core"
done <"$root/package/retroarch-mips/cores.lock"
