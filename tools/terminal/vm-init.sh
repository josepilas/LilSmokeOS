#!/bin/sh
exec </dev/console >/dev/console 2>&1
/bin/busybox --install -s /bin
export PATH=/bin:/usr/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /dev/shm /run /tmp
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=16m tmpfs /dev/shm
export HOME=/run SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software
export SDL_AUDIODRIVER=dummy LILSMOKE_DFB_EVDEV_KEYCODES=1
export LILSMOKE_SCREEN_WIDTH=640 LILSMOKE_SCREEN_HEIGHT=480
export DFBARGS='system=fbdev,fbdev=/dev/fb0,no-hardware,no-cursor,no-sighandler,quiet,no-vt,mode=640x480,depth=24,pixelformat=RGB24,disable-module=keyboard,linux-input-force'
grep MemTotal /proc/meminfo
/usr/bin/lilsmoke-terminal >/tmp/terminal.log 2>&1 &
terminal=$!
(
    while kill -0 "$terminal" 2>/dev/null; do
        for name in pty keyboard arrow ctrl-c; do
            ready=/run/$name-ok
            [ "$name" != pty ] || ready=/dev/pts/0
            [ "$name" != keyboard ] || ready=/run/k
            [ "$name" != arrow ] || ready=/run/a
            [ "$name" != ctrl-c ] || ready=/run/c
            if [ -e "$ready" ] && [ ! -e "/run/reported-$name" ]; then
                touch "/run/reported-$name"
                echo "QA_READY_$name"
            fi
        done
        sleep 0.1
    done
) &
wait "$terminal"
echo "QA_TERMINAL_EXIT=$?"
cat /tmp/terminal.log
echo QA_TERMINAL_VM_DONE
poweroff -f
