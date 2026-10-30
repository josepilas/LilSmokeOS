#!/bin/sh
set -eu
app=${LILSMOKE_APP_DIR:?LilSmoke Applications launcher required}
[ "${LILSMOKE_APP_ID:-}" = org.lilsmoke.pcsx4all ] || exit 2
[ ! -f /run/lilsmoke-storage/exporting ] || { echo 'Eject LILDRIVE first' >&2; exit 1; }
content=
loading_progress() {
    [ -n "${LILSMOKE_LOADING_PROGRESS:-}" ] || return 0
    temporary=$LILSMOKE_LOADING_PROGRESS.$$
    (umask 077; printf '%s\t%s\n' "$1" "$2" > "$temporary" &&
        mv -f "$temporary" "$LILSMOKE_LOADING_PROGRESS") || rm -f "$temporary"
    return 0
}
if [ "$#" -eq 2 ] && [ "$1" = --resolved ]; then
    # Private handoff from this binary's own menu; it has already resolved an archive.
    content=$2
elif [ -n "${core:-}" ]; then
    [ "$core" = psx ] || exit 2
    resolver=${LILSMOKE_ROM_RESOLVER:-/usr/lib/lilsmoke/lilsmoke-rom}
    export LILSMOKE_STATE_CONTENT="${LILSMOKE_STATE_CONTENT:-${LILSMOKE_ROM_ROOT:-/storage/roms}/psx/${rom:?ROM filename required}}"
    loading_progress 30 'Resolving ROM'
    content=$("$resolver" psx "${LILSMOKE_ROM_ROOT:-/storage/roms}/psx/${rom:?ROM filename required}") || exit $?
fi
[ -z "$content" ] || [ -f "$content" ] || { echo 'Game content missing' >&2; exit 4; }
export HOME=${LILSMOKE_OD_HOME_ROOT:-/persist/od-engines}/pcsx4all
export XDG_CONFIG_HOME="$HOME" XDG_DATA_HOME="$HOME" XDG_CACHE_HOME="$HOME"
export LILSMOKE_OD_NATIVE=1 LILSMOKE_OD_ENGINE=pcsx4all LILSMOKE_OD_SYSTEM=psx
# Preserve the working engine-owned evdev reader; never add a second input mapper.
export LILSMOKE_OD_INPUT_EXTERNAL=1 LILSMOKE_POINTER_ENABLED=0
export ROM_DIR="${LILSMOKE_ROM_ROOT:-/storage/roms}/psx" LILSMOKE_OD_ROM_DIR="${LILSMOKE_ROM_ROOT:-/storage/roms}/psx"
export BIOS_DIR=${LILSMOKE_BIOS_DIR:-/storage/bios}
logs=${LILSMOKE_LOG_DIR:-/persist/logs}
log_helper=${LILSMOKE_SESSION_LOG:-/usr/lib/lilsmoke/lilsmoke-session-log}
if [ -x "$log_helper" ]; then
    log=$("$log_helper" pcsx4all-app)
    logs=$(dirname "$log")
else log=$logs/pcsx4all-app.log; fi
config=${LILSMOKE_PCSX_CONFIG_ROOT:-/persist/config/pcsx4all}
mkdir -p "$HOME/saves" "$ROM_DIR" "$logs" "$config" /persist/saves/pcsx4all /persist/states/pcsx4all /persist/bios
if [ ! -f "$config/pcsx4all.cfg" ]; then
    cp "$app/runtime/defaults/pcsx4all.cfg" "$config/pcsx4all.cfg"
elif ! head -n 1 "$config/pcsx4all.cfg" | grep -q '^CONFIG_VERSION 0$'; then
    mv "$config/pcsx4all.cfg" "$config/pcsx4all.cfg.legacy.$(date +%s)"
    cp "$app/runtime/defaults/pcsx4all.cfg" "$config/pcsx4all.cfg"
fi
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software
export SDL12COMPAT_OPENGL_SCALING=0 SDL12COMPAT_SYNC_TO_VBLANK=0 SDL12COMPAT_SCALE_METHOD=nearest
export SDL12COMPAT_ALLOW_THREADED_PUMPS=0 SDL12COMPAT_ALLOW_THREADED_DRAWS=0
export LILSMOKE_DFB_EVDEV_KEYCODES=1 SDL_RENDER_SCALE_QUALITY=nearest
unset DISPLAY WAYLAND_DISPLAY LD_AUDIT
export LD_LIBRARY_PATH="$app/runtime/lib:/storage/emulators/lib:/usr/lib:/lib"
LD_PRELOAD=
if [ "${LILSMOKE_M8_LOADING_GAME:-0}" = 1 ] &&
   [ "${LILSMOKE_M8_BOOT_STATE:-}" = /run/m8-boot-logo.state ]; then
    LD_PRELOAD=/os/libm8-boot-logo.so
fi
if [ -r /usr/lib/lilsmoke/libod-sdl.so ]; then LD_PRELOAD=${LD_PRELOAD:+$LD_PRELOAD:}/usr/lib/lilsmoke/libod-sdl.so; fi
if [ "${LILSMOKE_HCFB:-0}" = 1 ]; then
    [ -r /os/libm8-display.so ] && [ -r /usr/lib/lilsmoke/libod-sdl.so ] || { echo 'Native device adapters missing' >&2; exit 3; }
    LD_PRELOAD=${LD_PRELOAD:+$LD_PRELOAD:}/os/libm8-display.so
    export SDL_AUDIODRIVER=dummy LILSMOKE_M8_NATIVE_VIDEO=1 LILSMOKE_M8_AUDIO=1
else
    case "${LILSMOKE_AUDIO_OVERRIDE:-alsa}" in
        alsa|dummy) export SDL_AUDIODRIVER=${LILSMOKE_AUDIO_OVERRIDE:-alsa} ;;
        *) exit 2 ;;
    esac
fi
if [ "${LILSMOKE_M8_AUDIO:-0}" = 1 ]; then
    reader=${LILSMOKE_CONFIG_READER:-/usr/lib/lilsmoke/lilsmoke-config-read}
    volume=$("$reader" MASTER_VOLUME 50 2>/dev/null) || volume=50
    case "$volume" in ''|*[!0-9]*|????*) volume=50 ;; esac
    volume=$(printf '%s\n' "$volume" | awk '{v=$0+0; if(v>100)v=100; print int(v/2)}')
    export LILSMOKE_M8_VOLUME="$volume"
fi
if [ -x /usr/bin/lilsmoke-bt ] && [ -r /persist/config/bluetooth-audio ]; then
    IFS= read -r audio_mac </persist/config/bluetooth-audio || audio_mac=
    bt_pcm=$(/usr/bin/lilsmoke-bt audio "$audio_mac" 2>/dev/null) || bt_pcm=
    case "$bt_pcm" in bluealsa:DEV=*,PROFILE=a2dp) export ALSA_NAME="$bt_pcm" ;; esac
fi
export LD_PRELOAD
export DFBARGS="no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,${LILSMOKE_DFB_PIXEL_OPTIONS:-depth=32,pixelformat=ARGB}"
export LILSMOKE_OD_SHARE_DIR="$app/data"
mkdir -p "$app/data"
cd "$app/data"
set --
if [ -n "$content" ]; then
    case "$content" in *.bin|*.img) [ ! -f "${content%.*}.cue" ] || content=${content%.*}.cue ;; esac
    set -- "$content"
fi
printf 'PCSX4ALL_APP_LAUNCH menu=%s\n' "$( [ -z "$content" ] && echo 1 || echo 0 )" >> "$log"
loading_progress 70 'Starting emulator'
exec "$app/runtime/bin/pcsx4all" "$@" >> "$log" 2>&1
