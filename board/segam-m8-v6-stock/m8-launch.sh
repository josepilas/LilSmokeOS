#!/bin/sh
set -u
umask 077
BASE=/mnt/sdcard/lilsmoke
STOCK=/mnt/sdcard/cubegm/icube.stock
ROOT=/tmp/lilsmoke-m8-root
LOCK=/tmp/lilsmoke-m8-launch.lock
[ ! -f /tmp/lilsmoke-m8-shutdown ] || exit 0
mkdir "$LOCK" 2>/dev/null || exit 1
printf '%s\n' "$$" > "$LOCK/owner" || exit 1
boot_log=$BASE/bringup.log
if ! ( : >> "$boot_log" ) 2>/dev/null; then
    mkdir -p /media/mmcblk0p2/.lilsmoke-diagnostics 2>/dev/null || true
    boot_log=/media/mmcblk0p2/.lilsmoke-diagnostics/bringup.log
    if ! ( : >> "$boot_log" ) 2>/dev/null; then boot_log=/tmp/lilsmoke-bringup.log; fi
fi
exec >> "$boot_log" 2>&1
echo "=== LilSmokeOS M8 candidate startup $(date -u) ==="
sync
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
child=''
logo_child=''
loading_dir=$LOCK/loading
logo_minimum=1
loading_disabled=no
mounted_root=no
growth_complete=no
shutting_down=no

cleanup() {
    # Cancel a first-frame wait before signaling SDL (which can handle TERM).
    rm -f "$ROOT/run/m8-boot-logo.state" "$LOCK/boot-logo.state"
    rm -f "$loading_dir/active"
    if [ -n "$logo_child" ]; then
        kill -TERM "$logo_child" 2>/dev/null || true
        wait "$logo_child" 2>/dev/null || true
        logo_child=''
    fi
    if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; fi
    child=''
    if [ "$mounted_root" = yes ]; then
        chroot "$ROOT" /etc/init.d/S07usb-gamepads stop 2>/dev/null || true
        sync
        for path in run/m8-loading dev/pts dev/shm persist storage lilos os dev sys proc; do umount "$ROOT/$path" 2>/dev/null || true; done
        umount "$ROOT" 2>/dev/null || true
    fi
    if [ "$shutting_down" = no ]; then
        rm -f "$loading_dir/active" "$loading_dir/stopped" "$loading_dir/progress" "$loading_dir/progress.new"
        rmdir "$loading_dir" 2>/dev/null || true
        rm -f "$LOCK/owner"
        rmdir "$LOCK" 2>/dev/null || true
    fi
}

loading_progress() {
    [ -d "$loading_dir" ] || return 0
    printf '%s\t%s\n' "$1" "$2" > "$loading_dir/progress.new" &&
        mv "$loading_dir/progress.new" "$loading_dir/progress" || true
}

start_loading() {
    state=$1
    minimum=$2
    [ -x "$BASE/m8-boot-logo" ] && [ -s "$BASE/libm8-boot-logo.so" ] && [ -s "$BASE/boot-logo.argb" ] || return 0
    if [ -n "$logo_child" ]; then
        rm -f "$loading_dir/active"
        kill -TERM "$logo_child" 2>/dev/null || true
        wait "$logo_child" 2>/dev/null || true
        logo_child=''
    fi
    mkdir -p "$loading_dir" || return 0
    rm -f "$state" "$loading_dir/stopped"
    : > "$loading_dir/active"
    loading_progress 2 'Preparing system'
    if [ "$minimum" = yes ]; then
        "$BASE/m8-boot-logo" --show "$BASE/boot-logo.argb" "$state" --hold "$loading_dir" --owner "$$" &
    else
        "$BASE/m8-boot-logo" --show "$BASE/boot-logo.argb" "$state" --hold "$loading_dir" --no-minimum --owner "$$" &
    fi
    logo_child=$!
    attempt=0
    while [ ! -s "$state" ] && kill -0 "$logo_child" 2>/dev/null && [ "$attempt" -lt 40 ]; do
        sleep 0.05
        attempt=$((attempt + 1))
    done
    [ -s "$state" ] || echo 'Boot logo unavailable; continuing startup'
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fallback() {
    echo "FallbackReason=$*"
    cleanup
    trap - EXIT HUP INT TERM
    unset LD_PRELOAD
    export LD_LIBRARY_PATH=/mnt/sdcard/cubegm/lib:/mnt/sdcard/cubegm/usr/lib
    exec "$STOCK"
}

prepare_shutdown_guard() {
    # Stock rcK starts services again with "stop". Change only RAM, preserving
    # init's following swapoff/unmount actions. Close the supervisor first.
    export LILSMOKE_M8_SAFE_POWEROFF=0
    if [ ! -f /etc/init.d/rcK ]; then
        export LILSMOKE_M8_SAFE_POWEROFF=1
    elif grep -q 'lilsmoke-m8-shutdown' /etc/init.d/rcK; then
        export LILSMOKE_M8_SAFE_POWEROFF=1
    elif { printf '#!/bin/sh\nif [ -f /tmp/lilsmoke-m8-shutdown ]; then\n read -r pid < /tmp/lilsmoke-m8-launch.lock/owner\n case "$pid" in ""|0|1|*[!0-9]*) exit 1 ;; esac\n kill -TERM "$pid" 2>/dev/null || true\n sleep 1\n exit 0\nfi\n'; cat /etc/init.d/rcK; } > /etc/init.d/rcK.lilsmoke-new &&
         chmod 0755 /etc/init.d/rcK.lilsmoke-new &&
         mv /etc/init.d/rcK.lilsmoke-new /etc/init.d/rcK; then
        export LILSMOKE_M8_SAFE_POWEROFF=1
    else
        echo 'Safe shutdown unavailable: stock RAM guard could not be installed'
    fi
}

[ ! -f "$BASE/force-stock" ] || fallback 'force-stock requested'
"$BASE/m8-probe" || fallback 'native probe failed (CPU/ABI must be verified)'
sync
prepare_shutdown_guard
[ -e /dev/fb0 ] || fallback 'no Linux framebuffer device'
# Optional, static helper: storage/runtime preparation overlaps the logo hold.
# A missing/invalid payload must never disable stock recovery or normal boot.
start_loading "$LOCK/boot-logo.state" yes
loading_progress 5 'Checking ROM storage'
[ -b /dev/mmcblk0p2 ] || fallback 'LILROMS partition is not visible in the stock kernel'
"$BASE/m8-probe" --rom-volume /dev/mmcblk0p2 || fallback 'unexpected ROM partition label/filesystem'
if [ ! -f "$BASE/grown" ]; then
    # Stock mdev auto-mounts p2 before S99app. Release only the observed,
    # verified ROM-device mount; never force an unmount or touch p1.
    if awk '$3=="179:2" && $5=="/media/mmcblk0p2" {found=1} END {exit !found}' /proc/self/mountinfo; then
        if umount /media/mmcblk0p2; then
            echo 'Released stock ROM auto-mount for exclusive expansion'
        else
            echo 'Stock ROM auto-mount is busy; expansion will be deferred'
        fi
    fi
    if [ "$LILSMOKE_M8_SAFE_POWEROFF" = 1 ]; then
        "$BASE/m8-grow" /dev/mmcblk0
        growth=$?
    else
        echo 'Expansion deferred: safe restart preparation is unavailable'
        growth=2
    fi
    sync
    case "$growth" in
        0) growth_complete=yes ;;
        75)
            echo 'Expansion metadata verified; rebooting to refresh SD geometry'
            printf 'reboot\n' > /tmp/lilsmoke-m8-shutdown
            shutting_down=yes
            sync
            cleanup
            trap - EXIT HUP INT TERM
            # The stock M8 may expose /dev/console without a working console driver.
            exec >/dev/null 2>&1
            reboot
            while :; do sleep 3600; done
            ;;
        2|126|127) echo 'Expansion unavailable before metadata writes; starting UI with the existing ROM volume' ;;
        *) fallback 'ROM expansion stopped after metadata writes; inspect the log' ;;
    esac
fi

mkdir -p "$ROOT"
mount -t tmpfs -o size=16m,mode=0755,nosuid,nodev tmpfs "$ROOT" || fallback 'cannot create metadata-only RAM root'
mounted_root=yes
tab=$(printf '\t')
manifest_total=$(wc -l < "$BASE/runtime.tsv")
manifest_done=0
loading_progress 10 'Preparing runtime'
# Manifest paths are generated/validated by the image builder, not supplied by ROMs.
while IFS="$tab" read -r kind mode path target; do
    case "$path" in ''|/*|*..*|*\\*) fallback 'invalid runtime manifest path' ;; esac
    case "$kind" in
        D) mkdir -p "$ROOT/$path" && chmod "$mode" "$ROOT/$path" || fallback 'cannot create runtime directory' ;;
        F) ln -s "/os/files/$target" "$ROOT/$path" || fallback 'cannot link file-backed runtime object' ;;
        L) ln -s "$target" "$ROOT/$path" || fallback 'cannot reconstruct runtime symlink' ;;
        C) cp "$BASE/files/$target" "$ROOT/$path" && chmod "$mode" "$ROOT/$path" || fallback 'cannot prepare private account metadata' ;;
        *) fallback 'unknown runtime manifest record' ;;
    esac
    manifest_done=$((manifest_done + 1))
    if [ "$manifest_total" -gt 0 ] && [ "$((manifest_done % 32))" -eq 0 ]; then
        loading_progress "$((10 + 50 * manifest_done / manifest_total))" 'Preparing runtime'
    fi
done < "$BASE/runtime.tsv"
mkdir -p "$ROOT/run"
if [ -d "$loading_dir" ]; then
    mkdir -p "$ROOT/run/m8-loading"
    mount --bind "$loading_dir" "$ROOT/run/m8-loading" || echo 'Live loading progress unavailable'
fi
if [ -f "$LOCK/boot-logo.state" ]; then
    cp "$LOCK/boot-logo.state" "$ROOT/run/m8-boot-logo.state" || echo 'Boot logo timing token unavailable'
fi
for path in os dev sys proc lilos storage persist; do mkdir -p "$ROOT/$path"; done
loading_progress 65 'Mounting storage'
mount --bind "$BASE" "$ROOT/os" || fallback 'cannot expose file-backed OS objects'
mount --bind /mnt/sdcard "$ROOT/lilos" || fallback 'cannot expose LILOS'
mount --bind /dev "$ROOT/dev" || fallback 'cannot bind device nodes'
mount --bind /sys "$ROOT/sys" || fallback 'cannot bind sysfs'
mount --bind /proc "$ROOT/proc" || fallback 'cannot bind procfs'
mkdir -p "$ROOT/dev/pts"
# A non-recursive /dev bind does not expose the stock devpts submount.
mount -t devpts -o nosuid,noexec,gid=5,mode=620,ptmxmode=666 devpts "$ROOT/dev/pts" ||
    echo 'Warning: devpts unavailable; Shell Terminal will report its PTY error'
mkdir -p "$ROOT/dev/shm"
mount -t tmpfs -o size=32m,mode=1777,exec,nosuid,nodev tmpfs "$ROOT/dev/shm" || fallback 'cannot provide executable POSIX shared memory'
mount -t vfat -o rw,nosuid,nodev,utf8 /dev/mmcblk0p2 "$ROOT/storage" || fallback 'cannot mount LILROMS'
if [ "$growth_complete" = yes ]; then
    printf 'complete\n' > "$BASE/grown"
    sync
fi

# The real stock kernel has neither ext4 nor loop support. Keep settings on
# LILOS in that case; SSH stays off on filesystems without Unix permissions.
if grep -q 'ext4' /proc/filesystems && mount -t ext4 -o loop,rw "$BASE/persist.ext4" "$ROOT/persist"; then
    echo Persistence=ext4
else
    echo Persistence=FAT-bringup-fallback
    mkdir -p "$BASE/persist"
    mount --bind "$BASE/persist" "$ROOT/persist" || fallback 'cannot prepare persistence'
fi
mkdir -p "$ROOT/persist/config" "$ROOT/persist/logs" "$ROOT/persist/saves" "$ROOT/persist/states"
loading_progress 72 'Loading settings'
if [ ! -f "$ROOT/persist/config/lilsmoke.conf" ]; then
    cp "$BASE/initial.conf" "$ROOT/persist/config/lilsmoke.conf" || fallback 'cannot initialize settings'
fi
for system in nes snes gb gbc gba md sms gg pce psx n64 arcade lynx; do mkdir -p "$ROOT/storage/roms/$system"; done
mkdir -p "$ROOT/storage/bios" "$ROOT/storage/themes" "$ROOT/storage/independent-apps" \
    "$ROOT/storage/opendingux/apps"
# File-backed /os links are absolute: resolve them inside the runtime root.
if chroot "$ROOT" /bin/busybox test -x /usr/bin/lilsmoke-input-diagnostics; then
    loading_progress 75 'Checking controllers'
    chroot "$ROOT" /usr/bin/lilsmoke-input-diagnostics || echo 'Input diagnostics unavailable'
else
    echo 'Input diagnostics missing from runtime'
fi
if chroot "$ROOT" /bin/busybox test -x /etc/init.d/S07usb-gamepads; then
    loading_progress 77 'Starting controllers'
    echo 'Starting USB HIDAPI gamepad driver'
    chroot "$ROOT" /etc/init.d/S07usb-gamepads start || echo 'USB gamepad driver unavailable; native input retained'
else
    echo 'USB gamepad service missing from runtime'
fi
if chroot "$ROOT" /bin/busybox test -x /etc/init.d/S08applications; then
    loading_progress 79 'Loading applications'
    chroot "$ROOT" /etc/init.d/S08applications start || echo 'Builtin application seeding failed; inspect installer diagnostics'
fi

# The vendor kernel uses auddec/sndC0i2so, not ALSA. PocketSNES has an
# isolated native PCM worker; unadapted SDL applications remain silent.
unset DISPLAY WAYLAND_DISPLAY LD_PRELOAD LD_LIBRARY_PATH
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software
export SDL_AUDIODRIVER=dummy LILSMOKE_AUDIO_OVERRIDE=dummy
export LILSMOKE_M8_AUDIO=1
export LILSMOKE_UI_PROFILE=0
[ ! -f "$BASE/profile-enabled" ] || export LILSMOKE_UI_PROFILE=1
export LILSMOKE_DFB_PIXEL_OPTIONS='depth=32,pixelformat=ARGB'
export DFBARGS="no-hardware,no-cursor,$LILSMOKE_DFB_PIXEL_OPTIONS"
export LILSMOKE_HCFB=1
export LILSMOKE_STOCK_SUPERVISOR=1
export LILSMOKE_M8_LOADING_DIR=/run/m8-loading
export LILSMOKE_LOADING_PROGRESS=/run/m8-loading/progress
export LILSMOKE_CAPTURE_FRAME=/lilos/lilsmoke/boot-frame.ppm
if [ -f "$BASE/boot-frame.ppm" ]; then
    mv "$BASE/boot-frame.ppm" "$BASE/boot-frame.previous.ppm" || true
fi
echo NativeUIStarting
sync
runtime_failures=0
# No inherited or persistent warm hint may turn a true boot into a cache return.
unset LILSMOKE_UI_WARM_RETURN LILSMOKE_UI_SUPERVISOR_TOKEN
ui_warm_return=0
ui_generation=0
while :; do
    started=$(cut -d. -f1 /proc/uptime)
    ui_generation=$((ui_generation + 1))
    ui_token=$$:$ui_generation
    rm -f "$ROOT/run/lilsmoke-ui-app-handoff"
    preload=/os/libm8-display.so
    if [ ! -f "$ROOT/run/m8-boot-logo.state" ] && [ "$loading_disabled" = no ]; then
        logo_minimum=0
        start_loading "$ROOT/run/m8-boot-logo.state" no
    fi
    boot_logo_attempted=no
    if [ -f "$ROOT/run/m8-boot-logo.state" ] && [ -s "$BASE/libm8-boot-logo.so" ]; then
        preload=/os/libm8-boot-logo.so:$preload
        boot_logo_attempted=yes
    fi
    chroot "$ROOT" /usr/bin/env LD_PRELOAD="$preload" LILSMOKE_M8_BOOT_STATE=/run/m8-boot-logo.state \
        LILSMOKE_UI_SUPERVISOR_TOKEN="$ui_token" LILSMOKE_UI_WARM_RETURN="$ui_warm_return" \
        LILSMOKE_M8_LOADING_GAME="$((1 - logo_minimum))" /usr/bin/lilsmoke-ui &
    child=$!
    wait "$child"
    status=$?
    child=''
    ui_warm_return=0
    if [ -f "$ROOT/run/lilsmoke-ui-app-handoff" ] &&
       [ "$(cat "$ROOT/run/lilsmoke-ui-app-handoff")" = "$ui_token" ]; then
        ui_warm_return=1
    fi
    rm -f "$ROOT/run/lilsmoke-ui-app-handoff"
    if [ "$boot_logo_attempted" = yes ]; then
        if [ "$status" != 0 ] && { [ "$status" = 70 ] || [ -f "$ROOT/run/m8-boot-logo.state" ]; }; then
            loading_disabled=yes
        fi
        # A logo is optional. Never trap later UI restarts behind a bad gate.
        rm -f "$ROOT/run/m8-boot-logo.state" "$LOCK/boot-logo.state"
        rm -f "$loading_dir/active"
        logo_minimum=0
        [ "$status" = 0 ] || echo 'Boot-logo attempt ended; subsequent UI starts use the normal display path'
    fi
    if [ -f "$ROOT/run/lilsmoke-poweroff-request" ] &&
       [ "$(cat "$ROOT/run/lilsmoke-poweroff-request")" = poweroff ]; then
        echo 'Safe power off requested; flushing and releasing LilSmokeOS mounts'
        # Static helper must be in original-root RAM before releasing file-backed OS objects.
        if ! chroot "$ROOT" /bin/cat /usr/lib/lilsmoke/m8-safe-power > /tmp/lilsmoke-safe-power.new ||
           [ ! -s /tmp/lilsmoke-safe-power.new ] ||
           ! chmod 0755 /tmp/lilsmoke-safe-power.new ||
           ! mv /tmp/lilsmoke-safe-power.new /tmp/lilsmoke-safe-power; then
            echo 'Safe power off refused: cannot stage the RAM storage guard'
            rm -f "$ROOT/run/lilsmoke-poweroff-request"
            continue
        fi
        if [ "$(/tmp/lilsmoke-safe-power --self-check)" != 'M8_POWER_GUARD version=2' ]; then
            echo 'Safe power off refused: RAM storage guard failed its identity check'
            rm -f "$ROOT/run/lilsmoke-poweroff-request"
            continue
        fi
        printf 'poweroff\n' > /tmp/lilsmoke-m8-shutdown
        shutting_down=yes
        # /dev/console exists on this M8 but opening it returns ENODEV. Never let
        # that optional device abort the handoff. Close the SD log using /dev/null;
        # the RAM guard displays storage status directly through /dev/fb0.
        echo 'Safe power off: handing control to the console-independent RAM guard'
        exec >/dev/null 2>&1
        sync
        cleanup
        trap - EXIT HUP INT TERM
        swapoff -a || echo 'Warning: swapoff failed; storage guard will refuse a busy writable volume'
        # Replace the supervisor so its SD-backed mappings cannot pin the FAT.
        # The static RAM process owns final service stop, storage checks and display.
        exec /tmp/lilsmoke-safe-power --shutdown
        echo 'Terminal storage guard could not start; do not unplug the card'
        while :; do sleep 3600; done
    fi
    if [ -f "$ROOT/run/lilsmoke-poweroff-request" ]; then
        echo 'Ignoring malformed power-off request'
        rm -f "$ROOT/run/lilsmoke-poweroff-request"
    fi
    echo "NativeApplicationExited=$status"
    if [ "$status" = 0 ] || [ "$status" = 75 ]; then
        runtime_failures=0
        echo 'Returning to LilSmokeUI'
        sleep 1
    else
        ended=$(cut -d. -f1 /proc/uptime)
        # A user-selected application failure is not a frontend boot crash loop.
        if [ "$((ended-started))" -ge 5 ]; then runtime_failures=0; else
            runtime_failures=$((runtime_failures + 1))
        fi
        mkdir -p "$ROOT/storage/.lilsmoke-diagnostics" 2>/dev/null || true
        (dmesg | tail -n 160 > "$ROOT/storage/.lilsmoke-diagnostics/last-kernel.log") 2>/dev/null || true
        [ "$runtime_failures" -lt 3 ] || fallback "Repeated native application failures ($status); stock recovery retained"
        echo 'Application failed; returning to LilSmokeUI'
        sleep 1
    fi
done
