#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
out="$root/test-results/emulation-performance"
build=${1:-/root/lilsmoke-os/build/buildroot/output}
cp "$out/sdl2_gfx.c" "$build/build/retroarch-mips-v1.19.1/gfx/drivers/sdl2_gfx.c"
cp "$out/SDL_render_sw.c" "$build/build/sdl2-2.28.5/src/render/software/SDL_render_sw.c"
if [ -f "$out/SDL_stretch.c" ]; then
    cp "$out/SDL_stretch.c" "$build/build/sdl2-2.28.5/src/video/SDL_stretch.c"
fi
if [ -f "$out/SDL_DirectFB_events.c" ]; then
    cp "$out/SDL_DirectFB_events.c" "$build/build/sdl2-2.28.5/src/video/directfb/SDL_DirectFB_events.c"
fi
make -C "$build/build/retroarch-mips-v1.19.1" -j2
make -C "$build/build/sdl2-2.28.5" -j2
cp "$build/build/retroarch-mips-v1.19.1/retroarch" "$out/retroarch-candidate"
cp -L "$build/build/sdl2-2.28.5/build/.libs/libSDL2-2.0.so.0" "$out/libSDL2-candidate.so"
echo VIDEO_CANDIDATE_BUILT
