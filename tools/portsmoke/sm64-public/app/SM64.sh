#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
app=${LILSMOKE_APP_DIR:?LilSmoke Applications launcher required}
[ "${LILSMOKE_APP_ID:-}" = org.lilsmoke.sm64 ] || exit 2
[ ! -f /run/lilsmoke-storage/exporting ] || { echo 'Eject LILDRIVE first' >&2; exit 1; }
[ -x "$app/runtime/bin/sm64-prepare" ] && [ -f "$app/runtime/recipe.json" ] || {
    echo 'Incomplete SM64 application payload; preparation cannot start' >&2
    exit 3
}
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_POINTER_ENABLED=0 LILSMOKE_M8_AUDIO=1
unset DISPLAY WAYLAND_DISPLAY LD_AUDIT
if [ "${LILSMOKE_PRIVATE_HARDWARE_QA:-0}" != 1 ]; then
    unset SM64_ORIGINAL_TEST_FRAMES SM64_ORIGINAL_RENDER_EVERY SM64_ORIGINAL_EVIDENCE
    unset SM64_ORIGINAL_INTERACTIVE_AFTER SM64_ORIGINAL_INPUT_TRACE
    unset SM64_ORIGINAL_TEST_WIDTH_CYCLE SM64_ORIGINAL_TEST_WORLD_EVIDENCE
fi
unset SM64_ORIGINAL_PREFLIGHT
export LD_LIBRARY_PATH="$app/runtime/lib:/usr/lib:/lib"
LD_PRELOAD=
if [ "${LILSMOKE_HCFB:-0}" = 1 ]; then
    [ -r /os/libm8-display.so ] || { echo 'M8 display adapter missing' >&2; exit 3; }
    LD_PRELOAD=/os/libm8-display.so
fi
export LD_PRELOAD
export DFBARGS="no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,${LILSMOKE_DFB_PIXEL_OPTIONS:-depth=32,pixelformat=ARGB}"
reader=${LILSMOKE_CONFIG_READER:-/usr/lib/lilsmoke/lilsmoke-config-read}
volume=$("$reader" MASTER_VOLUME 50 2>/dev/null) || volume=50
case "$volume" in ''|*[!0-9]*|????*) volume=50 ;; esac
volume=$(printf '%s\n' "$volume" | awk '{v=$0+0;if(v>100)v=100;print int(v/2)}')
export LILSMOKE_M8_VOLUME="$volume"
mkdir -p "$app/original" "$app/saves"
log_helper=${LILSMOKE_SESSION_LOG:-/usr/lib/lilsmoke/lilsmoke-session-log}
if [ -x "$log_helper" ]; then log=$("$log_helper" sm64-public); else
    log=$app/saves/sm64-preparation.log
    if ! ( : >> "$log" ) 2>/dev/null; then log=/dev/null; fi
fi
options="$app/saves/.sm64-port/lilsmoke-options.conf"
if [ -f "$options" ]; then
    value=$(awk -F= '$1=="Quality" {print $2;exit}' "$options")
    case "$value" in detail|balanced|quality|performance|adaptive) export SM64_WORLD_QUALITY="$value" ;; esac
    value=$(awk -F= '$1=="Scale" {print $2;exit}' "$options")
    case "$value" in nearest|smooth) export SM64_WORLD_SCALE="$value" ;; esac
    value=$(awk -F= '$1=="Volume" {print $2;exit}' "$options")
    case "$value" in ''|*[!0-9]*|????*) ;; *)
        if [ "$value" -le 100 ]; then export SM64_APP_VOLUME="$value"; fi ;; esac
fi
# Virtual headroom includes compiler file mappings, not just resident RAM.
# The game drops to its separate 96 MiB virtual limit after worker/UI exit.
ulimit -v 131072
exec "$app/runtime/bin/sm64-prepare" "$app" >> "$log" 2>&1
