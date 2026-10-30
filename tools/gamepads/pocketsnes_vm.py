#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Private real-UI PocketSNES regression using the user's own 7z ROM."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from PIL import Image

P = Path(__file__).resolve().parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--rootfs', type=Path, required=True)
    ap.add_argument('--rom', type=Path, required=True)
    ap.add_argument('--backend', choices=('opendingux', 'retroarch'), default='opendingux')
    args = ap.parse_args()
    native = args.backend == 'opendingux'
    logfile = 'pocketsnes.log' if native else 'retroarch.log'
    out = args.out.resolve()
    out.mkdir(exist_ok=False)
    rom = out / 'storage/roms/snes/SMW-test.7z'
    rom.parent.mkdir(parents=True)
    shutil.copyfile(args.rom, rom)
    sys.path.insert(0, str(P / 'tools/hwtest'))
    sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs, disk_devices
    from vm_scene_smoke import Qmp
    init = """#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /storage /persist/config /persist/logs /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=32m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L storage /storage || poweroff -f
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_DFB_EVDEV_KEYCODES=1 LILSMOKE_AUDIO_OVERRIDE=dummy LILSMOKE_HCFB=0
export LILSMOKE_LOG_DIR=/bench
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,depth=24,pixelformat=RGB24
cat /proc/meminfo >/bench/memory.txt
(
    while :; do
        ps w >/bench/processes.txt
        for file in /proc/[0-9]*/comm; do
            read -r name < "$file" || continue
            case "$name" in lilsmoke-rom|pocketsnes|retroarch)
                cat "${file%/comm}/status" "${file%/comm}/wchan" "${file%/comm}/syscall" ;;
            esac
        done >/bench/launch-process.txt 2>&1
        sleep 2
    done
) &
# Match the real M8 supervisor: UI execs the emulator, then restarts on exit.
count=0
while [ "$count" -lt 2 ]; do
    /usr/bin/lilsmoke-ui >>/bench/ui.log 2>&1
    count=$((count+1))
done
sync
poweroff -f
"""
    overlay = Newc()
    overlay.file('usr/bin/bench-init', init, 0o755)
    overlay.file('persist/config/lilsmoke.conf',
        'DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\nSCAN_ROMS_ON_BOOT=on\n'
        'EMULATOR_BACKEND=' + args.backend + '\nEMULATOR_snes=' +
        ('od:pocketsnes' if native else 'ra:snes9x2005') + '\n' +
        'DEFAULT_THEME=default\nMASTER_VOLUME=50\nPOINTER_ENABLED=off\n')
    overlay.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
    overlay.file('etc/lilsmoke/input-maps/default.map',
        (P / 'board/qemu-mipsel/input-maps/default.map').read_bytes())
    overlay.file('usr/lib/lilsmoke/od-launch',
        (P / 'package/opendingux-emulators/src/od-launch').read_bytes(), 0o755)
    if not native:
        subprocess.run([str(B / 'host/bin/mipsel-buildroot-linux-musl-gcc'), '-shared', '-fPIC',
            '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            str(P / 'tools/gamepads/qa_fb_present.c'), '-o', str(out / 'present.so')], check=True)
        # Malta has no HC16xx scaler/audio devices. Exercise the real native
        # input/frontend path, with only scanout and absent audio substituted.
        overlay.file('usr/bin/retroarch-real', (B / 'target/usr/bin/retroarch').read_bytes(), 0o755)
        overlay.file('usr/bin/retroarch', '''#!/bin/sh
cfg=/tmp/lilsmoke-retroarch/retroarch.cfg
# RetroArch keeps the FIRST definition. Disable only hardware absent in Malta.
{ printf 'audio_enable = false\\npause_nonactive = false\\n'; cat "$cfg"; } >"$cfg.qa"
mv "$cfg.qa" "$cfg"
exec /usr/bin/retroarch-real "$@"
''', 0o755)
        init = init.replace('cat /proc/meminfo',
            'export LILSMOKE_HCFB=1 LD_PRELOAD=/bench/present.so LILSMOKE_QA_FB_PRESENT=1\n'
            'export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24,system=dummy\n'
            'export LILSMOKE_SCREEN_WIDTH=640 LILSMOKE_SCREEN_HEIGHT=480\ncat /proc/meminfo')
        # Replace the diagnostic init entry rather than adding a duplicate.
        next(entry for entry in overlay.entries if entry.name == 'usr/bin/bench-init').data = init.encode()
    initrd = out / 'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(B, overlay, '/usr/bin/bench-init', overlay_mib=16)))
    devices = [part.replace(str(B / 'images/rootfs.squashfs'), str(args.rootfs))
        for part in disk_devices(B, [('bench', out, False), ('storage', out / 'storage', False)])]
    command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
        '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120',
        '-kernel', str(B / 'images/vmlinux'), '-initrd', str(initrd),
        '-append', 'console=ttyS0 video=cirrusfb:640x480M-24@60 panic=-1',
        '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
        '-no-reboot', '-device', 'usb-kbd', '-qmp', f'unix:{out}/qmp.sock,server=on,wait=off', *devices]
    def read(name):
        path = out / name
        return path.read_text(errors='replace') if path.exists() else ''
    qmp = None
    with (out / 'serial.log').open('wb') as serial:
        vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=serial, stderr=subprocess.STDOUT)
        def wait_for(predicate, label, seconds=90):
            end = time.monotonic() + seconds
            while not predicate():
                if vm.poll() is not None or time.monotonic() > end:
                    raise RuntimeError(label + '\n' + read('ui.log')[-2000:] + '\n' + read(logfile)[-2000:])
                time.sleep(.3)
        try:
            wait_for(lambda: 'UI: entering main loop' in read('ui.log'), 'UI did not start')
            qmp = Qmp(out / 'qmp.sock')
            def key(*codes, delay=1):
                qmp.call('send-key', dict(keys=[dict(type='qcode', data=x) for x in codes], **{'hold-time': 180}))
                time.sleep(delay)
            def capture(name):
                end = time.monotonic() + 20
                while True:
                    qmp.call('screendump', dict(filename=str(out / (name + '.ppm'))))
                    with Image.open(out / (name + '.ppm')) as im:
                        if len(im.getcolors(im.width * im.height) or []) > 6:
                            im.save(out / (name + '.png'))
                            return
                    if time.monotonic() > end:
                        raise RuntimeError('Blank capture: ' + name)
                    time.sleep(.3)
            time.sleep(2)
            capture('01-snes-console')
            key('ret', delay=2)
            capture('02-rom-list')
            key('ret', delay=1)
            wait_for(lambda: (out / logfile).exists(), 'Emulator did not start', seconds=60)
            if not native:
                wait_for(lambda: 'QA_PRESENT_FRAME' in read(logfile), 'Core did not present a frame')
            time.sleep(8)
            capture('03-game-from-7z')
            key('ret', delay=5)
            key('right', delay=1)
            key('ctrl', delay=2)
            capture('04-start-and-buttons')
            key('ret', 'spc', delay=2)
            if native:
                wait_for(lambda: 'POCKETSNES_MENU_ENTER' in read(logfile), 'Start+X did not reach native menu')
            capture('05-native-menu')
            if native:
                key('alt', delay=2)
            else:
                key('ret', 'spc', delay=2)
            key('ret', 'esc', delay=2)
            wait_for(lambda: read('ui.log').count('UI: entering main loop') >= 2, 'Start+Select did not return UI')
            capture('06-return-home')
        finally:
            if qmp:
                qmp.close()
            if vm.poll() is None:
                vm.terminate()
            try:
                vm.wait(timeout=10)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait()
    assert int(re.search(r'SwapTotal:\s+(\d+)', read('memory.txt')).group(1)) == 0
    result = dict(passed=True, backend=args.backend, real_ui=True, selected_console_and_rom=True,
        compressed_7z=True, native_menu_and_exit=True, start_and_buttons_sent=True,
        cpus=1, ram_mib=120, swap_kib=0, physical_hc16xx=False, physical_fps_measured=False,
        native_scanout_substituted=not native, audio_device_not_emulated=True,
        emulator_sha256=hashlib.sha256((B / ('target/usr/lib/lilsmoke/od-emulators/pocketsnes'
            if native else 'target/usr/bin/retroarch')).read_bytes()).hexdigest())
    (out / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

