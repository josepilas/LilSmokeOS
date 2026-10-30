#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Real Settings -> PTY shell -> UI test in constrained MIPS/DirectFB."""
import argparse
import gzip
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from PIL import Image

P = Path(__file__).resolve().parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(exist_ok=False)
    sys.path.insert(0, str(P / 'tools/hwtest'))
    sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs, disk_devices
    from vm_scene_smoke import Qmp
    init = """#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /persist/config /persist/logs /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_DFB_EVDEV_KEYCODES=1 LILSMOKE_AUDIO_OVERRIDE=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,depth=24,pixelformat=RGB24
cat /proc/meminfo >/bench/memory.txt
/usr/bin/lilsmoke-ui >/bench/ui.log 2>&1
echo TERMINAL_UI_EXIT=$?
sync
poweroff -f
"""
    fixture = Newc()
    fixture.file('usr/bin/bench-init', init, 0o755)
    fixture.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
    fixture.file('etc/lilsmoke/input-maps/default.map',
        (P / 'board/qemu-mipsel/input-maps/default.map').read_bytes())
    fixture.file('persist/config/lilsmoke.conf',
        'DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\nSCAN_ROMS_ON_BOOT=off\n'
        'EMULATOR_BACKEND=retroarch\nDEFAULT_THEME=default\nMASTER_VOLUME=50\nPOINTER_ENABLED=off\n')
    root = out / 'rootfs.squashfs'
    subprocess.run([str(B / 'host/bin/mksquashfs'), str(B / 'target'), str(root),
        '-noappend', '-comp', 'gzip', '-processors', '1', '-no-xattrs'],
        check=True, stdout=subprocess.DEVNULL)
    initrd = out / 'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(B, fixture, '/usr/bin/bench-init', overlay_mib=16)))
    devices = [part.replace(str(B / 'images/rootfs.squashfs'), str(root))
        for part in disk_devices(B, [('bench', out, False)])]
    command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
        '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120',
        '-kernel', str(B / 'images/vmlinux'), '-initrd', str(initrd),
        '-append', 'console=ttyS0 video=cirrusfb:640x480M-24@60 panic=-1',
        '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio',
        '-monitor', 'none', '-no-reboot', '-device', 'usb-kbd',
        '-qmp', f'unix:{out}/qmp.sock,server=on,wait=off', *devices]
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
                    raise RuntimeError(label + '\n' + read('ui.log')[-3000:] + '\n' + read('serial.log')[-2000:])
                time.sleep(.2)
        try:
            wait_for(lambda: 'UI: entering main loop' in read('ui.log'), 'UI did not start')
            qmp = Qmp(out / 'qmp.sock')
            def key(code, delay=.3, shift=False, ctrl=False):
                codes = (['shift'] if shift else []) + (['ctrl'] if ctrl else []) + [code]
                qmp.call('send-key', dict(keys=[dict(type='qcode', data=x) for x in codes],
                    **{'hold-time': 80}))
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
            def type_command(text):
                for letter in text:
                    code = {' ': 'spc', '/': 'slash', '-': 'minus', '.': 'dot', '>': 'dot'}.get(letter, letter)
                    key(code, .15, shift=letter == '>')
                key('ret', 1)
            time.sleep(2)
            capture('01-home')
            # Empty scan: Applications, RetroArch, Explorer, OpenDingux, Settings, Exit.
            key('left')
            key('left')
            key('ctrl', 1)
            for _ in range(5):
                key('down')
            capture('02-settings-terminal')
            key('ctrl', 3)
            capture('03-shell')
            type_command('echo terminal-ok > /bench/proof')
            wait_for(lambda: 'terminal-ok' in read('proof'), 'Keyboard did not reach real shell')
            type_command('uname -m > /bench/architecture')
            type_command('tty > /bench/tty')
            type_command('stty size > /bench/size')
            assert read('architecture').strip() == 'mips'
            assert read('tty').strip().startswith('/dev/pts/')
            assert re.fullmatch(r'\d+ \d+\n', read('size'))
            type_command('sleep 30')
            key('c', 1, ctrl=True)
            type_command('echo interrupted > /bench/interrupt')
            wait_for(lambda: 'interrupted' in read('interrupt'), 'Ctrl-C did not interrupt foreground process')
            capture('04-real-shell-commands')
            type_command('exit')
            wait_for(lambda: read('ui.log').count('UI: entering main loop') >= 2, 'Shell exit did not return UI')
            capture('05-return-home')
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
    result = dict(passed=True, real_settings_launch=True, real_busybox_shell=True,
        real_pty=True, guest_keyboard=True, ctrl_c=True, return_to_ui=True,
        cpus=1, ram_mib=120, swap_kib=0, gpu=False, directfb=True,
        physical_hc16xx=False)
    (out / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

