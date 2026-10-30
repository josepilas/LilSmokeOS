#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -u
umask 077
BASE=/mnt/sdcard/lilsmoke
LOCK=/tmp/lilsmoke-m8-launch.lock
checkpoint() { "$BASE/r36-bootlog" "$*" || true; }
early_fail() {
    trap - EXIT
    checkpoint "EARLY_FAIL:$*"
    echo "R36_EARLY_FAILED=$*" >&2
    if [ "$(cat "$LOCK/owner" 2>/dev/null)" = "$$" ]; then
        rm -f "$LOCK/owner"
        rmdir "$LOCK" 2>/dev/null || true
    fi
    exit 70
}
if [ "${LILSMOKE_R36_RAM_LAUNCH:-0}" != 1 ]; then
    checkpoint SHELL_ENTRY
    # Competing entry paths must not overwrite an already-running RAM script.
    mkdir "$LOCK" 2>/dev/null || { checkpoint LOCK_BUSY; exit 73; }
    printf '%s\n' "$$" > "$LOCK/owner" || early_fail 'lock owner'
    trap 'early_fail "RAM shell failed"' EXIT
    # Keep one bounded previous startup log; do not accumulate logs indefinitely.
    if [ -f "$BASE/bringup.log" ]; then
        tail -c 131072 "$BASE/bringup.log" > "$BASE/bringup.previous.log" || true
    fi
    ( : > "$BASE/bringup.log" ) || early_fail 'startup log unwritable'
    cp "$BASE/r36-launch.sh" /tmp/lilsmoke-r36-launch.sh || early_fail 'RAM script copy'
    chmod 0700 /tmp/lilsmoke-r36-launch.sh || early_fail 'RAM script permission'
    LILSMOKE_R36_RAM_LAUNCH=1 exec /bin/sh /tmp/lilsmoke-r36-launch.sh
fi
checkpoint RAM_SHELL
cd / || early_fail 'working directory'
ROOT=/tmp/lilsmoke-r36-root
[ "$(cat "$LOCK/owner" 2>/dev/null)" = "$$" ] || early_fail 'RAM shell lock identity'
trap - EXIT
checkpoint LAUNCH_LOCKED
exec >> "$BASE/bringup.log" 2>&1
echo "=== LilSmoke R36SX 2.7 experimental startup $(date -u) ==="
dmesg | tail -c 65536 > "$BASE/kernel-early.log" 2>/dev/null || true
unset LD_PRELOAD
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
child=''
panel=''
ready_watch=''
mounted=no
stock_stopped=no
cleanup() {
    [ -z "$ready_watch" ] || { kill -TERM "$ready_watch" 2>/dev/null || true; wait "$ready_watch" 2>/dev/null || true; }
    [ -z "$child" ] || { kill -TERM "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; }
    [ -z "$panel" ] || { kill -TERM "$panel" 2>/dev/null || true; wait "$panel" 2>/dev/null || true; }
    if [ "$mounted" = yes ]; then
        chroot "$ROOT" /etc/init.d/S07usb-gamepads stop 2>/dev/null || true
        sync
        for p in tmp/joy_key dev/pts dev/shm persist storage lilos os dev sys proc; do
            umount "$ROOT/$p" 2>/dev/null || true
        done
        umount "$ROOT" 2>/dev/null || true
    fi
    if [ "$stock_stopped" = yes ]; then kill -CONT "$LILSMOKE_R36_STOCK_PID" 2>/dev/null || true; fi
    rm -f "$LOCK/owner"
    rmdir "$LOCK" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 143' TERM
trap 'exit 130' INT
trap 'exit 129' HUP
fail() {
    checkpoint "LAUNCH_FAILED:$*"
    dmesg | tail -c 65536 > "$BASE/kernel-failure.log" 2>/dev/null || true
    echo "R36_LAUNCH_FAILED=$*"
    exit 70
}
ram_power_guard() {
    cp "$BASE/m8-safe-power" /tmp/lilsmoke-safe-power.new || fail 'RAM power guard copy'
    chmod 0700 /tmp/lilsmoke-safe-power.new || fail 'RAM guard permission'
    mv /tmp/lilsmoke-safe-power.new /tmp/lilsmoke-safe-power || fail 'RAM guard commit'
    [ "$(/tmp/lilsmoke-safe-power --self-check)" = 'M8_POWER_GUARD version=2' ] || fail 'unexpected power guard'
    printf 'poweroff\n' > /tmp/lilsmoke-m8-shutdown
    checkpoint POWER_GUARD
    stock_stopped=no
    cleanup
    trap - EXIT HUP INT TERM
    sync
    swapoff -a || true
    exec >/dev/null 2>&1
    exec /tmp/lilsmoke-safe-power --shutdown
}
[ ! -f "$BASE/force-stock" ] || fail 'force-stock set; restore cubegm/setting.stock.xml to disable autorun'
if [ "${LILSMOKE_R36_BOOTSTRAP_MODE:-fork}" != exec ]; then
    case "${LILSMOKE_R36_STOCK_PID:-}" in ''|0|1|*[!0-9]*) fail 'invalid bootstrap parent' ;; esac
fi
case "$(cat /proc/device-tree/panel/status 2>/dev/null)" in okay) ;; *) fail 'not an enabled handheld panel' ;; esac
"$BASE/m8-probe" || fail 'native diagnostic failed'
"$BASE/m8-probe" --rom-volume /dev/mmcblk0p2 || fail 'LILROMS geometry/label missing'
checkpoint HARDWARE_PROBED
# Keep the actual vendor inode: Input::ftok must address cubevol's existing IPC.
pidof cubevol >/dev/null 2>&1 || { /usr/bin/cubevol & }
attempt=0
while [ ! -f /tmp/joy_key ] && [ "$attempt" -lt 40 ]; do sleep 0.05; attempt=$((attempt+1)); done
[ -f /tmp/joy_key ] || fail 'cubevol did not publish panel IPC'
if [ "${LILSMOKE_R36_IPC_KEY:--1}" != -1 ]; then
    "$BASE/r36-input" --check-key "$LILSMOKE_R36_IPC_KEY" || fail 'bootstrap IPC key changed'
fi
LILSMOKE_R36_IPC_KEY=$("$BASE/r36-input" --key) || fail 'cannot capture original vendor ftok key'
export LILSMOKE_R36_IPC_KEY
checkpoint INPUT_IPC_READY
if [ "${LILSMOKE_R36_BOOTSTRAP_MODE:-fork}" != exec ]; then
    kill -STOP "$LILSMOKE_R36_STOCK_PID" || fail 'cannot suspend stock renderer'
    stock_stopped=yes
fi
# Grow only the verified companion volume, using the existing guarded helper.
if [ ! -f "$BASE/grown" ]; then
    checkpoint GROW_BEGIN
    if awk '$3=="179:2" && $5=="/media/mmcblk0p2" {found=1} END {exit !found}' /proc/self/mountinfo; then
        umount /media/mmcblk0p2 || fail 'stock LILROMS mount busy'
    fi
    "$BASE/m8-grow" /dev/mmcblk0
    growth=$?
    case "$growth" in
        0) printf 'complete\n' > "$BASE/grown" ;;
        75) export LILSMOKE_R36_GEOMETRY_REBOOT=1; ram_power_guard ;;
        2) echo 'R36 growth deferred before writes' ;;
        *) fail 'growth did not finish safely' ;;
    esac
fi
checkpoint GROW_FINISHED
mkdir -p "$ROOT" || fail 'RAM root directory'
mount -t tmpfs -o size=12m,mode=0755,nosuid,nodev tmpfs "$ROOT" || fail 'RAM metadata root'
mounted=yes
tab=$(printf '\t')
while IFS="$tab" read -r kind mode path target; do
    case "$path" in ''|/*|*..*|*\\*) fail 'invalid runtime path' ;; esac
    case "$kind" in
        D) mkdir -p "$ROOT/$path" && chmod "$mode" "$ROOT/$path" || fail 'runtime directory' ;;
        F) ln -s "/os/files/$target" "$ROOT/$path" || fail 'file-backed object' ;;
        L) ln -s "$target" "$ROOT/$path" || fail 'runtime link' ;;
        C) cp "$BASE/files/$target" "$ROOT/$path" && chmod "$mode" "$ROOT/$path" || fail 'private runtime metadata' ;;
        *) fail 'unknown manifest record' ;;
    esac
done < "$BASE/runtime.tsv"
checkpoint RAM_ROOT_READY
for p in os dev sys proc lilos storage persist tmp dev/shm dev/pts; do mkdir -p "$ROOT/$p"; done
mount --bind "$BASE" "$ROOT/os" || fail 'file-backed payload bind'
mount --bind /mnt/sdcard "$ROOT/lilos" || fail 'LILOS bind'
for p in dev sys proc; do mount --bind "/$p" "$ROOT/$p" || fail "bind $p"; done
mount -t tmpfs -o size=8m,mode=1777,exec,nosuid,nodev tmpfs "$ROOT/dev/shm" || fail 'POSIX shm'
mount -t devpts -o nosuid,noexec,gid=5,mode=620,ptmxmode=666 devpts "$ROOT/dev/pts" || echo 'R36 PTY unavailable'
: > "$ROOT/tmp/joy_key"
mount --bind /tmp/joy_key "$ROOT/tmp/joy_key" || fail 'vendor IPC inode bind'
chroot "$ROOT" /os/r36-input --check-key "$LILSMOKE_R36_IPC_KEY" || fail 'chroot changed vendor ftok key'
mount -t vfat -o rw,nosuid,nodev,utf8 /dev/mmcblk0p2 "$ROOT/storage" || fail 'LILROMS mount'
checkpoint STORAGE_MOUNTED
mkdir -p "$BASE/persist"
mount --bind "$BASE/persist" "$ROOT/persist" || fail 'persistence bind'
mkdir -p "$ROOT/persist/config" "$ROOT/persist/logs" "$ROOT/persist/saves" "$ROOT/persist/states"
if [ ! -f "$ROOT/persist/config/lilsmoke.conf" ]; then
    cp "$BASE/initial.conf" "$ROOT/persist/config/lilsmoke.conf" || fail 'initial handheld configuration'
fi
mkdir -p "$ROOT/storage/roms" "$ROOT/storage/independent-apps" "$ROOT/storage/packages"
for service in S07usb-gamepads S08applications; do
    chroot "$ROOT" /etc/init.d/"$service" start || echo "R36 optional service unavailable: $service"
done
chroot "$ROOT" /os/r36-input --broker /run/lilsmoke-gamepads.state /run/lilsmoke-r36-gamepads.state &
panel=$!
sleep 0.1
kill -0 "$panel" 2>/dev/null || fail 'panel/USB gamepad transport unavailable'
checkpoint INPUT_BROKER_READY
export LILSMOKE_GAMEPAD_STATE=/run/lilsmoke-r36-gamepads.state
# No vendor glibc path may leak across this exec boundary into musl.
unset DISPLAY WAYLAND_DISPLAY LD_LIBRARY_PATH LD_PRELOAD
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_AUDIO_OVERRIDE=dummy LILSMOKE_M8_AUDIO=1
export LILSMOKE_HCFB=1 LILSMOKE_R36_DISPLAY=1 LILSMOKE_UI_PROFILE=0
export LILSMOKE_SCREEN_WIDTH=640 LILSMOKE_SCREEN_HEIGHT=480
export LILSMOKE_STOCK_SUPERVISOR=1 LILSMOKE_M8_SAFE_POWEROFF=1
export DFBARGS=no-hardware,no-cursor,depth=32,pixelformat=ARGB
export LILSMOKE_BOOT_READY_FILE=/run/lilsmoke-ui-ready
failures=0
while :; do
    rm -f "$ROOT/run/lilsmoke-ui-ready"
    checkpoint UI_EXEC
    chroot "$ROOT" /usr/bin/env LD_PRELOAD=/os/libr36-display.so /usr/bin/lilsmoke-ui &
    child=$!
    (
        trap - EXIT HUP INT TERM
        attempts=0
        while kill -0 "$child" 2>/dev/null && [ "$attempts" -lt 180 ]; do
            if [ -f "$ROOT/run/lilsmoke-ui-ready" ]; then
                checkpoint UI_FIRST_PRESENT
                exit 0
            fi
            sleep 1
            attempts=$((attempts+1))
        done
        [ "$attempts" -lt 180 ] || checkpoint UI_FIRST_PRESENT_TIMEOUT
    ) &
    ready_watch=$!
    wait "$child"
    result=$?
    child=''
    kill -TERM "$ready_watch" 2>/dev/null || true
    wait "$ready_watch" 2>/dev/null || true
    ready_watch=''
    checkpoint "UI_EXIT:$result"
    if [ -f "$ROOT/run/lilsmoke-poweroff-request" ] &&
       [ "$(cat "$ROOT/run/lilsmoke-poweroff-request")" = poweroff ]; then
        unset LILSMOKE_R36_GEOMETRY_REBOOT
        ram_power_guard
    fi
    echo "R36_UI_EXIT=$result"
    if [ "$result" = 0 ] || [ "$result" = 75 ]; then failures=0; else failures=$((failures+1)); fi
    [ "$failures" -lt 3 ] || fail 'repeated UI failures; inspect bringup.log'
    sleep 1
done
