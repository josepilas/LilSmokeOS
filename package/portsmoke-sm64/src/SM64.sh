#!/bin/sh
# Independent native gameplay prototype; not the complete SM64 game.
set -eu
umask 077

fail() { printf 'SM64 BYO Data Development: %s\n' "$*" >&2; exit 1; }
APP_DIR=${LILSMOKE_APP_DIR:?Launch through LilSmoke Applications}
[ ! -L "$APP_DIR" ] || fail 'The application directory must not be a symlink.'
cd -- "$APP_DIR"
APP_DIR=$PWD

[ ! -L ./original ] || fail 'The original ROM directory must not be a symlink.'
[ ! -L ./.sm64 ] || fail 'The private cache directory must not be a symlink.'
[ ! -L ./saves ] || fail 'The save directory must not be a symlink.'
mkdir -p ./original ./.sm64 ./saves
export LD_LIBRARY_PATH="$APP_DIR/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export HOME="$APP_DIR/saves" XDG_DATA_HOME="$APP_DIR/saves" XDG_CONFIG_HOME="$APP_DIR/saves"
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software
export DFBARGS="${DFBARGS-no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,depth=32,pixelformat=ARGB}"
unset DISPLAY WAYLAND_DISPLAY
# Preserve Applications' LD_PRELOAD and M8 input/video/PCM opt-ins; original
# ROM sound effects use SDL callbacks through the OS's native PCM adapter.
# ROM-derived Mario/terrain and original animation; no N64 emulator fallback.
exec "$APP_DIR/runtime/bin/sm64-scene-dev" --original "$APP_DIR/original" --pack "$APP_DIR/.sm64/bob-us-v1.pdat" --icon "$APP_DIR/icon.webp"
