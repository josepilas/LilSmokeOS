#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""First-run extraction/resume plus its real SDL UI, in 120 MiB MIPS Linux.

This deliberately cancels before the large game compilation. It is not an
HC16xx timing benchmark, a full build test, or a successful cache commit.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
P = HERE.parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(exist_ok=False)
    app = out / 'Test App'
    runtime = app / 'runtime'
    shutil.copytree(args.runtime, runtime, ignore=shutil.ignore_patterns('corresponding-source', '__pycache__'), symlinks=True)
    for name in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py'):
        shutil.copyfile(HERE / name, runtime / name)
    shutil.copyfile('/root/lilsmoke-public-bootstrap/sm64-prepare', runtime / 'bin/sm64-prepare')
    (runtime / 'bin/sm64-prepare').chmod(0o755)
    (app / 'original').mkdir()
    shutil.copyfile(args.rom, app / 'original/Owner ROM.z64')
    original = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    icon = args.runtime.parent / 'icon.webp'
    shutil.copyfile(icon if icon.is_file() else P / 'package/portsmoke-sm64/src/icon.webp', app / 'icon.webp')
    disk = out / 'application.fat'
    with disk.open('wb') as image:
        image.truncate(384 * 1024 * 1024)
    subprocess.run(['mkfs.vfat', '-F', '32', str(disk)], check=True, capture_output=True)
    subprocess.run(['mcopy', '-s', '-i', str(disk), str(app), '::/'], check=True)
    sys.path.insert(0, str(P / 'tools/hwtest'))
    sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs, disk_devices, runtime_libraries
    from vm_scene_smoke import Qmp
    init = '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /portdisk /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t vfat -o rw,umask=000 /dev/vdb /portdisk || poweroff -f
mount -t tmpfs -o size=8m tmpfs /dev/shm
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,depth=24,pixelformat=RGB24
export LILSMOKE_APP_DIR='/portdisk/Test App' LILSMOKE_HCFB=0
export LD_LIBRARY_PATH='/portdisk/Test App/runtime/lib:/usr/lib:/lib'
export LILSMOKE_DFB_EVDEV_KEYCODES=1
cat /proc/meminfo > /bench/memory.txt
(while :; do
    sleep 2
    for comm in /proc/[0-9]*/comm; do
        read name < "$comm" || continue
        case "$name" in python3.11|sm64-prepare|cc1)
            cat "${comm%/comm}/status" 2>/dev/null ;;
        esac
    done
done) > /bench/process.log 2>&1 &
ulimit -v 92160
for run in 1 2; do
    '/portdisk/Test App/runtime/bin/sm64-prepare' '/portdisk/Test App' > "/bench/preparation-$run.log" 2>&1
    echo QA_PREPARATION_EXIT_$run=$?
    sleep 1
done
sync
umount /portdisk
poweroff -f
'''
    overlay = Newc()
    overlay.file('usr/bin/bench-init', init, 0o755)
    binary = Path('/root/lilsmoke-public-bootstrap/sm64-prepare')
    runtime_libraries(B, overlay, [binary])
    overlay.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
    # The rootfs image can predate the current input maps. Exercise the same
    # binding as the board, not whichever keyboard layout that image contains.
    overlay.file('etc/lilsmoke/input-maps/default.map',
                 (P / 'board/qemu-mipsel/input-maps/default.map').read_bytes())
    initrd = out / 'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(B, overlay, '/usr/bin/bench-init', overlay_mib=16)))
    command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
               '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120',
               '-kernel', str(B / 'images/vmlinux'), '-initrd', str(initrd),
               '-append', 'console=ttyS0 video=cirrusfb:640x480M-16@60 panic=-1',
               '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
               '-no-reboot', '-device', 'usb-kbd',
               '-qmp', f'unix:{out}/qmp.sock,server=on,wait=off',
               *disk_devices(B, [('bench', out, False)]),
               '-drive', f'file={disk},if=none,id=appdisk,format=raw',
               '-device', 'virtio-blk-pci,drive=appdisk,addr=0x14.2']
    qmp = None
    unpack_seen = cancelled = resumed_cancelled = False
    started = time.monotonic()
    with (out / 'serial.log').open('wb') as serial:
        vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=serial, stderr=subprocess.STDOUT)
        try:
            while vm.poll() is None:
                log = out / 'preparation-1.log'
                text = log.read_text(errors='replace') if log.exists() else ''
                second = out / 'preparation-2.log'
                resumed = second.read_text(errors='replace') if second.exists() else ''
                if qmp is None and (out / 'qmp.sock').exists():
                    qmp = Qmp(out / 'qmp.sock')
                if qmp and not unpack_seen and 'SM64_STAGE unpack_begin' in text:
                    time.sleep(1)
                    qmp.call('screendump', dict(filename=str(out / 'unpacking.ppm')))
                    unpack_seen = True
                if qmp and not cancelled and 'SM64_STAGE checkpoint_ready' in text:
                    time.sleep(0.2)
                    qmp.call('screendump', dict(filename=str(out / 'assets-extracted.ppm')))
                    qmp.call('send-key', dict(keys=[dict(type='qcode', data='alt')], **{'hold-time':2000}))
                    cancelled = True
                if qmp and not resumed_cancelled and 'SM64_STAGE resume_assets' in resumed:
                    import re
                    if any(int(n) > 0 for n in re.findall(r'build_progress completed=(\d+)', resumed)):
                        qmp.call('screendump', dict(filename=str(out / 'resumed-build.ppm')))
                        qmp.call('send-key', dict(keys=[dict(type='qcode', data='alt')], **{'hold-time':2000}))
                        resumed_cancelled = True
                if 'reboot: System halted' in (out / 'serial.log').read_text(errors='replace'):
                    break
                if time.monotonic() - started > 1800:
                    raise RuntimeError('First-run extraction VM timed out')
                time.sleep(0.2)
        finally:
            if qmp:
                qmp.close()
            if vm.poll() is None:
                vm.terminate()
            try:
                vm.wait(timeout=5)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait()
    text = (out / 'preparation-1.log').read_text(errors='replace')
    resumed = (out / 'preparation-2.log').read_text(errors='replace')
    assert 'SM64_STAGE normalized_rom_moved' in text
    assert 'SM64_STAGE assets_done' in text
    assert 'QA_PREPARATION_EXIT_1=130' in (out / 'serial.log').read_text(errors='replace')
    assert 'QA_PREPARATION_EXIT_2=130' in (out / 'serial.log').read_text(errors='replace')
    assert 'SM64_STAGE resume_assets' in resumed and 'SM64_STAGE unpack_begin' not in resumed
    assert resumed_cancelled
    readback = out / 'rom-readback.z64'
    subprocess.run(['mcopy', '-o', '-i', str(disk), '::/Test App/original/Owner ROM.z64', str(readback)], check=True)
    assert original == hashlib.sha256(readback.read_bytes()).hexdigest()
    committed = subprocess.run(['mdir', '-i', str(disk), '::/Test App/.sm64-cache/current.json'], capture_output=True)
    assert committed.returncode != 0
    from PIL import Image
    for name in ('unpacking', 'assets-extracted', 'resumed-build'):
        with Image.open(out / (name + '.ppm')) as frame:
            frame.save(out / (name + '.png'))
    report = dict(passed=True, cpu='74Kf-lilsmoke', cpus=1, ram_mib=120,
                  process_limit_kib=92160, real_rom_extraction=True,
                  production_extraction_ui=True, cancel_preserves_original=True,
                  resumes_without_reextracting=True, real_build_progress=True,
                  application_filesystem='FAT32', full_game_compiled=False, physical_hc16xx=False)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
