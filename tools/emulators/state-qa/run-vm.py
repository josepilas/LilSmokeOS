"""Run real engine state tests in an isolated one-CPU, 120 MiB MIPS guest."""
import gzip
from pathlib import Path
import subprocess
import sys
import time

P = Path(__file__).resolve().parents[3]
B = Path('/root/lilsmoke-os/build/buildroot/output')
Q = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(P / 'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import disk_devices, runtime_libraries

overlay = Newc()
init = '''#!/bin/sh
export PATH=/bin:/usr/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs dev /dev
mkdir -p /bench /dev/shm /run /persist /tmp
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mkdir -p /bench/states
export SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy HOME=/bench
cat /proc/meminfo > /bench/memory.txt
/bench/pcsx-roundtrip /bench/states > /bench/pcsx-roundtrip.log 2>&1
echo $? > /bench/pcsx-roundtrip.exit
if [ -x /bench/libretro-roundtrip ]; then
    /bench/libretro-roundtrip /usr/lib/libretro/fceumm_libretro.so > /bench/libretro-roundtrip.log 2>&1
    echo $? > /bench/libretro-roundtrip.exit
fi
if [ -x /bench/pcsx-menu-ui ]; then
    export SDL_VIDEODRIVER=directfb
    export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,system=dummy,depth=32,pixelformat=ARGB
    for mode in surface renderer; do
        /bench/pcsx-menu-ui "$mode" > /bench/pcsx-menu-ui-$mode.log 2>&1
        echo $? > /bench/pcsx-menu-ui-$mode.exit
    done
fi
cat /bench/*roundtrip.log
sync
poweroff -f
'''
overlay.file('init', init, 0o755)
for name in ('bin/busybox', 'lib/libc.so', 'lib/ld-musl-mipsel.so.1'):
    overlay.file(name, (B / 'target' / name).read_bytes(), 0o755)
for name in subprocess.check_output(['busybox', '--list'], text=True).splitlines():
    overlay.symlink('bin/' + name, 'busybox')
core = B / 'target/usr/lib/libretro/fceumm_libretro.so'
overlay.file('usr/lib/libretro/fceumm_libretro.so', core.read_bytes(), 0o755)
runtime_libraries(B, overlay, [Q / 'pcsx-roundtrip', core])
if (Q / 'pcsx-menu-ui').exists():
    modules = list((B / 'target/usr/lib/directfb-1.7-7').rglob('*.so'))
    for module in modules:
        overlay.file(str(module.relative_to(B / 'target')), module.read_bytes(), 0o755)
    runtime_libraries(B, overlay, [Q / 'pcsx-menu-ui', *modules])
    overlay.file('usr/share/fonts/dejavu/DejaVuSans.ttf', (B / 'target/usr/share/fonts/dejavu/DejaVuSans.ttf').read_bytes())
root = Newc()
parents = {str(parent) for entry in overlay.entries for parent in Path(entry.name).parents if str(parent) != '.'}
parents.update(('dev', 'proc', 'sys', 'tmp', 'run', 'persist'))
for name in sorted(parents, key=lambda name: (name.count('/'), name)):
    root.dir(name)
root.chrdev('dev/console', 5, 1)
root.entries += overlay.entries
(Q / 'boot.cpio.gz').write_bytes(gzip.compress(root.build()))
command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
           '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120',
           '-kernel', str(B / 'images/vmlinux'), '-initrd', str(Q / 'boot.cpio.gz'),
           '-append', 'console=ttyS0 panic=-1', '-vga', 'none', '-display', 'none',
           '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
           *disk_devices(B, [('bench', Q, False)])]
with (Q / 'serial.log').open('wb') as log:
    vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 120
        while vm.poll() is None and 'reboot: System halted' not in (Q / 'serial.log').read_text(errors='replace'):
            if time.monotonic() > deadline:
                raise RuntimeError('state QA VM timeout: ' + str(Q))
            time.sleep(.2)
    finally:
        if vm.poll() is None:
            vm.terminate()
        try:
            vm.wait(timeout=10)
        except subprocess.TimeoutExpired:
            vm.kill()
            vm.wait(timeout=10)
for engine in ('pcsx', 'libretro'):
    exitfile = Q / (engine + '-roundtrip.exit')
    if engine == 'libretro' and not (Q / 'libretro-roundtrip').exists():
        continue
    assert exitfile.read_text().strip() == '0', (Q / (engine + '-roundtrip.log')).read_text(errors='replace')
    print((Q / (engine + '-roundtrip.log')).read_text(errors='replace')[-1800:])
print('PASS: actual engine serialization, CPU-only MIPS, 120 MiB, isolated persistent files:', Q)
if (Q / 'pcsx-menu-ui').exists():
    for mode in ('surface', 'renderer'):
        log = (Q / ('pcsx-menu-ui-' + mode + '.log')).read_text(errors='replace')
        assert (Q / ('pcsx-menu-ui-' + mode + '.exit')).read_text().strip() == '0', log[-4000:]
        assert 'PCSX_PAUSE_UI_PASS mode=' + mode in log
        print(log[-1000:])
