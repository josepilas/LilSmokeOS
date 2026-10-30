#!/bin/sh
# Video comparison baseline: input safety fixes, original software formats.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
build=${1:-/root/lilsmoke-os/build/buildroot/output}
out="$root/test-results/emulation-performance"
source="$build/build/sdl2-2.28.5/src/render/software/SDL_render_sw.c"
stretch="$build/build/sdl2-2.28.5/src/video/SDL_stretch.c"
cmp "$source" "$out/SDL_render_sw.c"
cmp "$stretch" "$out/SDL_stretch.c"
trap 'cp "$out/SDL_render_sw.c" "$source"; cp "$out/SDL_stretch.c" "$stretch"' EXIT
cp "$out/SDL_render_sw.original.c" "$source"
cp "$out/SDL_stretch.original.c" "$stretch"
make -C "$build/build/sdl2-2.28.5" -j2
cp -L "$build/build/sdl2-2.28.5/build/.libs/libSDL2-2.0.so.0" "$out/libSDL2-baseline-safe.so"
echo VIDEO_BASELINE_BUILT
