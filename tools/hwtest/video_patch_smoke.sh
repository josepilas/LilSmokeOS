#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build=${BUILDROOT_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
out=$(mktemp -d /root/lilsmoke-performance/patch-smoke-XXXXXX)
tar -xzf "$build/../dl/sdl2/SDL2-2.28.5.tar.gz" -C "$out" --strip-components=1 \
    SDL2-2.28.5/src/render/software/SDL_render_sw.c \
    SDL2-2.28.5/src/video/SDL_stretch.c \
    SDL2-2.28.5/src/video/directfb/SDL_DirectFB_events.c
for patch_file in "$root"/patches/sdl2/*.patch; do
    patch --batch --forward -d "$out" -p1 <"$patch_file"
done
mkdir -p "$out/gfx/drivers"
cp "$root/test-results/emulation-performance/sdl2_gfx.original.c" "$out/gfx/drivers/sdl2_gfx.c"
for patch_file in "$root"/package/retroarch-mips/*.patch; do
    patch --batch --forward -d "$out" -p1 <"$patch_file"
done
diff --strip-trailing-cr "$out/src/render/software/SDL_render_sw.c" "$root/test-results/emulation-performance/SDL_render_sw.c"
diff --strip-trailing-cr "$out/src/video/SDL_stretch.c" "$root/test-results/emulation-performance/SDL_stretch.c"
diff --strip-trailing-cr "$out/src/video/directfb/SDL_DirectFB_events.c" "$root/test-results/emulation-performance/SDL_DirectFB_events.c"
diff --strip-trailing-cr "$out/gfx/drivers/sdl2_gfx.c" "$root/test-results/emulation-performance/sdl2_gfx.c"
echo VIDEO_PACKAGE_PATCHES_MATCH_TESTED_SOURCE
