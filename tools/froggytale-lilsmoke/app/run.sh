#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd -- "$HERE"
export LD_LIBRARY_PATH="$HERE/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_RENDER_DRIVER=software SDL_RENDER_OPENGL_SHADERS=0
export LILSMOKE_POINTER_ENABLED=0
export FROGGY_SAVE_DIR="${FROGGY_SAVE_DIR:-$HERE/saves}"
export FROGGY_CACHE_DIR="${FROGGY_CACHE_DIR:-$HERE/cache}"
# The installed OS owns the native display/audio driver ABI, not this package.
if [ "${LILSMOKE_HCFB:-0}" = 1 ]; then
    test -r /os/libm8-display.so
    test -r /usr/lib/lilsmoke/libod-sdl.so
    export LD_PRELOAD=/usr/lib/lilsmoke/libod-sdl.so:/os/libm8-display.so
    export LILSMOKE_OD_NATIVE=1 LILSMOKE_OD_ENGINE=independent LILSMOKE_OD_INPUT_EXTERNAL=1 LILSMOKE_M8_AUDIO=1
    export SDL_VIDEODRIVER=directfb SDL_AUDIODRIVER=dummy
else
    unset LD_PRELOAD
fi
ulimit -v 114688
exec "$HERE/runtime/bin/froggytale-lilsmoke" "$@"
