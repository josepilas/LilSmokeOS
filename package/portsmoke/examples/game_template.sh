#!/bin/sh
#
# PortSmoke game script template.
#
# Copy this into /storage/portsmoke/ports/<game>/<GameName>.sh and update
# GAME_ID, ENGINE_BIN, and the engine argument list below.

set -u
umask 022

GAME_ID="${PORTSMOKE_GAME:-example}"
GAME_DIR="${PORTSMOKE_GAME_DIR:-$(pwd)}"
ENGINE_DIR="${PORTSMOKE_ENGINE_DIR:-/mnt/portsmoke/example-engine}"
SAVE_DIR="${PORTSMOKE_SAVE_DIR:-/storage/portsmoke/saves/$GAME_ID}"
RUNTIME_DIR="${PORTSMOKE_RUNTIME_DIR:-/tmp/portsmoke/$GAME_ID}"

ENGINE_BIN="$ENGINE_DIR/bin/example-engine"
set -- \
    "--data-dir" "$GAME_DIR" \
    "--save-dir" "$SAVE_DIR"

die() {
    echo "$GAME_ID: $*" >&2
    exit 1
}

save_bind_mounted=no
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    if [ "$save_bind_mounted" = yes ]; then
        umount "$GAME_DIR/saves" >/dev/null 2>&1 || true
    fi
    exit "$status"
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

export SDL_VIDEODRIVER="directfb"
export SDL_AUDIODRIVER="alsa"
export SDL_RENDER_DRIVER="software"
export DFBARGS="${DFBARGS:-no-hardware,no-sighandler,quiet}"
export LIBGL_ALWAYS_SOFTWARE=1
unset DISPLAY
unset WAYLAND_DISPLAY

if [ -d "$ENGINE_DIR/lib" ]; then
    if [ -n "${LD_LIBRARY_PATH:-}" ]; then
        export LD_LIBRARY_PATH="$ENGINE_DIR/lib:$LD_LIBRARY_PATH"
    else
        export LD_LIBRARY_PATH="$ENGINE_DIR/lib"
    fi
fi

mkdir -p "$SAVE_DIR" "$SAVE_DIR/home" "$SAVE_DIR/config" "$SAVE_DIR/cache" \
    "$RUNTIME_DIR" || die "cannot create runtime directories"
export HOME="$SAVE_DIR/home"
export XDG_CONFIG_HOME="$SAVE_DIR/config"
export XDG_CACHE_HOME="$SAVE_DIR/cache"
export TMPDIR="$RUNTIME_DIR"

cd "$GAME_DIR" || die "cannot enter game directory: $GAME_DIR"

# FAT32 does not support symlinks. Prefer the requested saves symlink, then
# transparently fall back to a bind mount when the game lives on FAT storage.
if [ -L saves ]; then
    rm -f saves || die "cannot replace stale saves link"
elif [ -e saves ]; then
    [ -d saves ] || die "$GAME_DIR/saves exists and is not a directory"
    [ -z "$(find saves -mindepth 1 -print -quit 2>/dev/null)" ] || \
        die "$GAME_DIR/saves is not empty"
fi

if [ ! -e saves ] && ln -s "$SAVE_DIR" saves 2>/dev/null; then
    :
else
    [ -d saves ] || mkdir -p saves || die "cannot create saves mount point"
    mount --bind "$SAVE_DIR" saves || die "filesystem has no symlink support and bind mount failed"
    save_bind_mounted=yes
fi

[ -x "$ENGINE_BIN" ] || die "engine binary missing or not executable: $ENGINE_BIN"

"$ENGINE_BIN" "$@"
exit $?
