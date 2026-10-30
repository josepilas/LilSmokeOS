#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build=${BUILDROOT_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
src="$build/build/retroarch-mips-v1.19.1"
out="$root/test-results/emulation-performance"
cp "$out/sdl2_gfx.original.c" "$src/gfx/drivers/sdl2_gfx.c"
patch -d "$src" -p1 <"$root/package/retroarch-mips/0001-sdl2-bound-viewport-readback.patch"
make -C "$src" -j2 >"$out/readback-build.log" 2>&1
cp "$src/retroarch" "$out/retroarch-readback"
echo RETROARCH_READBACK_BUILT
