#!/usr/bin/env python3
"""Fixed-work full-system MIPS display comparison, never physical-console FPS."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs, disk_devices

ROOT = Path(__file__).resolve().parents[2]

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    p.add_argument('--qemu', required=True)
    p.add_argument('--disc', type=Path, required=True)
    p.add_argument('--frontend', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frames', type=int, default=180)
    p.add_argument('--repeats', type=int, default=2)
    p.add_argument('--shm', action='store_true', help='enable production shared-memory setup in every case')
    p.add_argument('--shm-matrix', action='store_true', help='compare renderer, surface, and surface plus mirrored memory')
    p.add_argument('--config', type=Path, help='use the same explicit native core configuration in every case')
    a = p.parse_args()
    if not 1 <= a.frames <= 10000 or not 1 <= a.repeats <= 10: p.error('invalid workload')
    out = a.out.resolve(); out.mkdir(parents=True, exist_ok=False)
    if len(str(out/'qmp.sock').encode()) >= 100: p.error('use a short output path')
    script = '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs dev /dev
mkdir -p /bench /rom /persist /dev/pts
mkdir -p /persist/saves/pcsx4all /persist/config/pcsx4all /persist/states/pcsx4all /persist/bios
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,ro rom /rom || poweroff -f
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software
export DFBARGS=no-hardware,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_DFB_EVDEV_KEYCODES=1
unset DISPLAY WAYLAND_DISPLAY
echo QA_MEMORY
cat /proc/meminfo
cat > /persist/config/pcsx4all/pcsx4all.cfg <<'EOF'
CONFIG_VERSION 0
ShowFps 0
FrameLimit 0
FrameSkip 0
EOF
'''
    if a.shm: script += '/etc/init.d/S03shm start || poweroff -f\n'
    if a.config:
        script += 'cp /bench/pcsx4all.cfg /persist/config/pcsx4all/pcsx4all.cfg\n'
        (out/'pcsx4all.cfg').write_bytes(a.config.read_bytes())
    names = []
    for repetition in range(a.repeats):
        modes = ['renderer', 'surface', 'surface-shm'] if a.shm_matrix else ['renderer', 'surface']
        if repetition % 2: modes.reverse()
        for mode in modes:
            name = f'{repetition+1}-{mode}'; names.append(name)
            if a.shm_matrix:
                script += '/etc/init.d/S03shm start || poweroff -f\n' if mode.endswith('-shm') else 'umount /dev/shm 2>/dev/null || true\n'
            video_mode = 'surface' if mode.startswith('surface') else 'renderer'
            script += f'LILSMOKE_BENCH_FRAMES={a.frames} LILSMOKE_BENCH_GAMEPLAY=1 LILSMOKE_PSX_VIDEO={video_mode} /usr/bin/pcsx4all-launch "/rom/{a.disc.name}" >/bench/{name}.log 2>&1\n'
            script += f'echo CASE {name} rc=$?\ntail -3 /bench/{name}.log\n'
    script += 'echo BENCH_END\nsync\npoweroff -f\n'
    overlay = Newc(); overlay.file('usr/bin/bench-init', script, 0o755)
    overlay.file('etc/init.d/S03shm', (ROOT/'package/lilsmoke-scripts/src/S03shm').read_bytes(), 0o755)
    overlay.file('usr/bin/pcsx4all-launch', a.frontend.read_bytes(), 0o755)
    overlay.file('usr/lib/libSDL2-2.0.so.0.2800.5', (a.build/'target/usr/lib/libSDL2-2.0.so.0.2800.5').read_bytes(), 0o755)
    overlay.file('etc/directfbrc', (ROOT/'board/qemu-mipsel/directfbrc').read_bytes())
    initrd = out/'benchmark.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(a.build,overlay,'/usr/bin/bench-init')))
    cmd = [a.qemu, '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-accel', 'tcg,thread=single', '-m', '128',
           '-kernel', str(a.build/'images/vmlinux'), '-initrd', str(initrd), '-append', 'console=ttyS0 video=cirrusfb:856x480M-24@60 panic=-1',
           '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
           '-device', 'usb-kbd', '-audiodev', 'driver=none,id=audio', '-device', 'AC97,audiodev=audio']
    cmd += disk_devices(a.build,[('bench',out,False),('rom',a.disc.parent.resolve(),True)])
    manifest = dict(command=cmd, frames=a.frames, repeats=a.repeats, shm=a.shm, shm_matrix=a.shm_matrix,
                    frontend_sha256=hashlib.sha256(a.frontend.read_bytes()).hexdigest(),
                    rootfs_sha256=hashlib.sha256((a.build/'images/rootfs.squashfs').read_bytes()).hexdigest(),
                    rootfs_mode='disk SquashFS with small diagnostic overlay',
                    disc_sha256=hashlib.sha256(a.disc.read_bytes()).hexdigest(),
                    input='Start 200..214; Cross 650..664,1200..1249; Up 1000..1099; Circle 1400..1449',
                    notice='Identical PSX core/workload with frame-based input. TCG throughput, not physical timing. No encoder.')
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2))
    with (out/'serial.log').open('wb') as log:
        vm = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 2400
            while 'BENCH_END' not in (out/'serial.log').read_text(errors='replace'):
                if vm.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('VM benchmark failed; inspect serial.log')
                time.sleep(1)
        finally:
            if vm.poll() is None: vm.terminate()
            try: vm.wait(timeout=10)
            except subprocess.TimeoutExpired: vm.kill(); vm.wait()
    results = []
    for name in names:
        text = (out/(name+'.log')).read_text(errors='replace')
        match = re.search(r'PCSX_BENCH frames=(\d+) elapsed_ms=(\d+) video_ms=(\d+) method=(\w+) hash=(\w+)', text)
        if not match or int(match[1]) != a.frames: raise RuntimeError(f'{name}: incomplete workload')
        results.append(dict(case=name, frames=int(match[1]), seconds=int(match[2])/1000, video_seconds=int(match[3])/1000,
                            presented_fps=1000*a.frames/int(match[2]), method=match[4], hash=match[5]))
    if len({row['hash'] for row in results}) != 1: raise RuntimeError('display variants differ in final PSX pixels')
    (out/'results.json').write_text(json.dumps(results, indent=2)); print(json.dumps(results, indent=2))

if __name__ == '__main__': main()
