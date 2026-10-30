"""Actual RetroArch/FCEUmm menu in a private 120 MiB MIPS guest (QMP keys only)."""
import argparse
import gzip
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time

P = Path(__file__).resolve().parents[3]
B = Path('/root/lilsmoke-os/build/buildroot/output')
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--rom', type=Path, required=True)
ap.add_argument('--core', default='fceumm_libretro.so')
ap.add_argument('--system', default='nes')
ap.add_argument('--file-backed', action='store_true')
ap.add_argument('--core-link', action='store_true')
ap.add_argument('--info-link', action='store_true')
ap.add_argument('--user-config', type=Path)
ap.add_argument('--binary', type=Path, default=B / 'target/usr/bin/retroarch')
ap.add_argument('--test-states', action='store_true')
a = ap.parse_args()
Q = a.out.resolve()
Q.mkdir(exist_ok=False)
if not re.fullmatch(r'[a-z0-9_]+_libretro\.so', a.core) or not re.fullmatch(r'[a-z0-9]+', a.system):
    raise ValueError('Invalid core or system identifier')
content_name = 'StateQA' + a.rom.suffix
shutil.copyfile(a.rom, Q / content_name)
sys.path.insert(0, str(P / 'tools/hwtest'))
sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs, disk_devices, runtime_libraries
from vm_scene_smoke import Qmp

init = '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /storage /persist/config /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_DFB_EVDEV_KEYCODES=1 LILSMOKE_HCFB=1 LILSMOKE_AUDIO_OVERRIDE=dummy
export LILSMOKE_QA_FB_PRESENT=1
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=16,pixelformat=RGB16
export LILSMOKE_SESSION_LOG=/run/not-installed LILSMOKE_LOG_DIR=/bench
export LILSMOKE_STATE_ROOT=/bench/states LILSMOKE_SAVE_ROOT=/bench/saves
export LILSMOKE_CONFIG=/bench/system.conf LILSMOKE_ROM_RESOLVER=/usr/lib/lilsmoke/lilsmoke-rom
printf 'DISPLAY_WIDTH=856\nDISPLAY_HEIGHT=480\nSCALING_MODE=aspect\n' > /bench/system.conf
cat /proc/meminfo > /bench/memory.txt
echo QA_RGUI_START
/usr/lib/lilsmoke/lilsmoke-retroarch run nes /bench/StateQA.nes
echo QA_RGUI_EXIT=$?
sync
poweroff -f
'''
init = init.replace('run nes /bench/StateQA.nes',
                    f'run {a.system} /bench/{content_name} {a.core}')
overlay = Newc()
subprocess.run([str(B / 'host/bin/mipsel-buildroot-linux-musl-gcc'), '-O2', '-shared', '-fPIC',
    str(P / 'tools/gamepads/qa_fb_present.c'), '-o', str(Q / 'present.so')], check=True)
overlay.file('usr/bin/bench-init', init, 0o755)
overlay.file('usr/bin/retroarch-real', a.binary.read_bytes(), 0o755)
overlay.file('usr/bin/retroarch', '#!/bin/sh\nexport LD_PRELOAD=/bench/present.so\nexec /usr/bin/retroarch-real "$@"\n', 0o755)
for name in ('lilsmoke-retroarch',):
    overlay.file('usr/lib/lilsmoke/' + name, (P / 'package/lilsmoke-scripts/src' / name).read_bytes(), 0o755)
overlay.file('usr/lib/lilsmoke/lilsmoke-rom', (B / 'target/usr/lib/lilsmoke/lilsmoke-rom').read_bytes(), 0o755)
core = B / 'target/usr/lib/libretro' / a.core
core_path = 'usr/lib/libretro/' + a.core
info_name = a.core.removesuffix('.so') + '.info'
info_path = 'usr/share/libretro/info/' + info_name
if a.file_backed or a.core_link:
    overlay.file('os/files/000001.bin', core.read_bytes(), 0o644)
    overlay.symlink(core_path, '/os/files/000001.bin')
else:
    overlay.file(core_path, core.read_bytes(), 0o755)
if a.file_backed or a.info_link:
    overlay.file('os/files/000002.bin', (B / 'target' / info_path).read_bytes())
    overlay.symlink(info_path, '/os/files/000002.bin')
else:
    overlay.file(info_path, (B / 'target' / info_path).read_bytes())
if a.user_config:
    overlay.file('persist/config/retroarch.cfg', a.user_config.read_bytes())
overlay.file('etc/retroarch.cfg', (P / 'package/retroarch-mips/retroarch.cfg').read_bytes())
overlay.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
runtime_libraries(B, overlay, [a.binary, core, B / 'target/usr/lib/lilsmoke/lilsmoke-rom'])
(Q / 'boot.cpio.gz').write_bytes(gzip.compress(overlay_initramfs(B, overlay, '/usr/bin/bench-init', overlay_mib=32)))
command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel', '-M', 'malta',
    '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120', '-kernel', str(B / 'images/vmlinux'),
    '-initrd', str(Q / 'boot.cpio.gz'), '-append', 'console=ttyS0 video=cirrusfb:856x480M-16@60 panic=-1',
    '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
    '-device', 'usb-kbd', '-qmp', f'unix:{Q}/qmp.sock,server=on,wait=off',
    *disk_devices(B, [('bench', Q, False)])]
with (Q / 'serial.log').open('wb') as log:
    vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
    qmp = None
    try:
        deadline = time.monotonic() + 600
        while vm.poll() is None:
            if time.monotonic() > deadline:
                raise RuntimeError('RGUI test timed out')
            if (Q / 'stop').exists():
                break
            if (Q / 'ready-for-menu').exists():
                time.sleep(.2)
                continue
            if qmp is None and (Q / 'qmp.sock').exists():
                try:
                    qmp = Qmp(Q / 'qmp.sock')
                except ConnectionRefusedError:
                    continue
            if qmp and (Q / 'retroarch.log').exists() and 'content_history.lpl' in (Q / 'retroarch.log').read_text(errors='replace').lower():
                time.sleep(2)
                qmp.key('home')
                time.sleep(3)
                qmp.call('screendump', dict(filename=str(Q / 'menu.ppm')))
                if a.test_states:
                    for _ in range(4):
                        qmp.key('down')
                        time.sleep(.4)
                    qmp.key('ctrl')
                    end = time.monotonic() + 20
                    while not list((Q / 'states').rglob('*.state')):
                        if time.monotonic() > end:
                            raise RuntimeError('Quick Menu did not create a save state')
                        time.sleep(.2)
                    time.sleep(1)
                    qmp.key('down')
                    time.sleep(.5)
                    qmp.key('ctrl')
                    end = time.monotonic() + 20
                    while '[State]: Loading state ' not in (Q / 'retroarch.log').read_text(errors='replace'):
                        if time.monotonic() > end:
                            raise RuntimeError('Quick Menu did not load the state')
                        time.sleep(.2)
                    time.sleep(2)
                    qmp.call('screendump', dict(filename=str(Q / 'state-actions.ppm')))
                    (Q / 'report.json').write_text(json.dumps(dict(
                        passed=True, core=a.core, system=a.system,
                        file_backed=a.file_backed, actual_quick_menu_buttons=True,
                        save_file_bytes=sum(f.stat().st_size for f in (Q / 'states').rglob('*.state')),
                        actual_mips_kernel=True, cpus=1, ram_mib=120,
                        physical_console_tested=False), indent=2) + '\n')
                    print('RGUI save/load PASS: ' + str(Q), flush=True)
                    break
                qmp.close()
                qmp = None
                (Q / 'ready-for-menu').touch()
                print('RGUI ready: ' + str(Q), flush=True)
            time.sleep(.2)
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
