#!/bin/sh
set -eu
build=${1:-/root/lilsmoke-os/build/buildroot/output}
ra="$build/build/retroarch-mips-v1.19.1"
grep -nE 'max-frames|Average|average' "$ra/retroarch.c" "$ra/runloop.c"
grep -nE 'angle ==|SDL_RenderCopyEx|SDL_RenderCopyF' "$build/build/sdl2-2.28.5/src/render/SDL_render.c"
grep -nE 'SLOW|BlitScaled|Stretch|CONVERT' "$build/build/sdl2-2.28.5/src/render/software/SDL_render_sw.c"
grep -nE 'sinc|resampler|quality' "$ra/config.def.h" | head -35
grep -nE 'frameskip|RenderThisFrame' "$(dirname "$0")/../../package/retroarch-mips/cores/snes9x2005/libretro.c" | head -45
"$build/host/bin/mipsel-buildroot-linux-musl-gcc" --version | head -1
