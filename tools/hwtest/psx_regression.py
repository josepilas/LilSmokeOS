#!/usr/bin/env python3
"""Actual MIPS OS/core regression. Malta timing is not physical M8 FPS."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs, disk_devices

P = Path(__file__).resolve().parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')
Q = Path('/root/lilsmoke-psx-20261010')
sys.path.insert(0,str(P/'tools/portsmoke/sm64'))
from vm_scene_smoke import Qmp


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--play-controls',action='store_true')
    args = parser.parse_args()
    assert 100 <= args.frames <= 6000
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    core = Path('/root/lilsmoke-emulator-expansion-20261008/built-libretro/pcsx_rearmed_libretro.so')
    benchmark = out / 'core-benchmark'
    subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'), '-std=gnu11', '-O2',
                    '-I'+str(P/'package/retroarch-mips/src/retroarch/libretro-common/include'),
                    str(P/'tools/hwtest/core_benchmark.c'), '-ldl', '-o', str(benchmark)], check=True)
    script = f'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mount -t devpts devpts /dev/pts
mkdir -p /bench /rom /persist/config/pcsx4all /persist/saves/pcsx4all /persist/states/pcsx4all /persist/bios
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,ro rom /rom || poweroff -f
/etc/init.d/S03shm start || poweroff -f
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_DFB_EVDEV_KEYCODES=1
cp /etc/pcsx4all.cfg /persist/config/pcsx4all/pcsx4all.cfg
sed -i 's/^FrameLimit .*/FrameLimit 0/' /persist/config/pcsx4all/pcsx4all.cfg
cat /proc/meminfo > /bench/memory.txt
cd /bench
LILSMOKE_BENCH_FRAMES={args.frames} LILSMOKE_BENCH_GAMEPLAY=1 LILSMOKE_BENCH_CAPTURE=/bench/native.ppm /usr/bin/pcsx4all-launch '/rom/{args.disc.name}' > native.log 2>&1
echo NATIVE_RESULT=$?
tail -5 native.log
LILSMOKE_BENCH_INPUT=1 LILSMOKE_BENCH_PSX={'1' if args.play_controls else ''} ./core-benchmark /usr/lib/libretro/pcsx_rearmed_libretro.so '/rom/{args.disc.name}' {args.frames} 1 > libretro.log 2>&1
echo LIBRETRO_RESULT=$?
tail -8 libretro.log
echo PSX_REGRESSION_DONE
sync
poweroff -f
'''
    overlay = Newc()
    overlay.file('usr/bin/bench-init', script, 0o755)
    overlay.file('usr/bin/pcsx4all-launch', (Q/'pcsx4all-launch').read_bytes(), 0o755)
    overlay.file('usr/lib/libretro/pcsx_rearmed_libretro.so', core.read_bytes(), 0o644)
    overlay.file('etc/pcsx4all.cfg', (P/'package/pcsx4all/pcsx4all.cfg').read_bytes())
    overlay.file('etc/init.d/S03shm', (P/'package/lilsmoke-scripts/src/S03shm').read_bytes(), 0o755)
    overlay.file('etc/directfbrc', (P/'board/qemu-mipsel/directfbrc').read_bytes())
    overlay.file('usr/lib/libSDL2-2.0.so.0.2800.5', (B/'target/usr/lib/libSDL2-2.0.so.0.2800.5').read_bytes(), 0o755)
    initrd = out/'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(B, overlay, '/usr/bin/bench-init')))
    command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
               '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-accel', 'tcg,thread=single',
               '-m', '120', '-kernel', str(B/'images/vmlinux'), '-initrd', str(initrd),
               '-append', 'console=ttyS0 video=cirrusfb:640x480M-16@60 panic=-1',
               '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
               '-audiodev', 'driver=none,id=audio', '-device', 'AC97,audiodev=audio']
    if args.play_controls:
        command+=['-device','usb-kbd','-qmp',f'unix:{out}/qmp.sock,server=on,wait=off']
    command += disk_devices(B, [('bench', out, False), ('rom', args.disc.parent.resolve(), True)])
    with (out/'serial.log').open('wb') as log:
        vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        qmp=None;performed=[]
        actions=[(1600,'ctrl',500),(2100,'ctrl',500),(2600,'up',2500),(3000,'ctrl',500),(3400,'shift',500)]
        try:
            deadline = time.monotonic()+1200
            while 'PSX_REGRESSION_DONE' not in (out/'serial.log').read_text(errors='replace'):
                if vm.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('guest test failed; inspect serial.log')
                if args.play_controls:
                    if qmp is None and (out/'qmp.sock').exists():qmp=Qmp(out/'qmp.sock')
                    native_log=(out/'native.log').read_text(errors='replace') if (out/'native.log').exists() else ''
                    progress=re.findall(r'PCSX_VIDEO frames=(\d+)',native_log)
                    current=int(progress[-1]) if progress else 0
                    while qmp and len(performed)<len(actions) and current>=actions[len(performed)][0]:
                        at,key,hold=actions[len(performed)]
                        qmp.call('send-key',dict(keys=[dict(type='qcode',data=key)],**{'hold-time':hold}))
                        performed.append(dict(frame=at,key=key,hold_ms=hold))
                time.sleep(2)
        finally:
            if qmp:qmp.close()
            if vm.poll() is None:
                vm.terminate()
            try:
                vm.wait(timeout=10)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait()
    serial = (out/'serial.log').read_text(errors='replace')
    assert 'NATIVE_RESULT=0' in serial and 'LIBRETRO_RESULT=0' in serial, serial[-3000:]
    if args.play_controls:assert len(performed)==len(actions),performed
    colors = {}
    for name in ('native.ppm', 'last-frame.ppm'):
        raw = (out/name).read_bytes().split(b'\n', 3)[3]
        colored = sum(max(raw[i:i+3])-min(raw[i:i+3]) > 12 for i in range(0, len(raw), 3))
        assert colored > 100, f'{name}: blank/grayscale output'
        colors[name] = colored
    result = dict(passed=True, frames=args.frames, colors=colors, cpus=1, ram_mib=120,
                  native_sha256=hashlib.sha256((Q/'pcsx4all-launch').read_bytes()).hexdigest(),
                  libretro_sha256=hashlib.sha256(core.read_bytes()).hexdigest(),
                  physical_m8_tested=False, physical_fps_claimed=False,
                  native_guest_usb_input=performed,libretro_gameplay_input=args.play_controls,
                  native_vendor_scaler_tested=False, command=command)
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
