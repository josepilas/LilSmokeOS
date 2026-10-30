"""Real-disc state tests with kernel-enforced missing MIPS syscalls, no UI input."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import sys
import time
import os

P = Path(__file__).resolve().parents[3]
B = Path('/root/lilsmoke-os/build/buildroot/output')
Q = Path(os.environ.get('PCSX_QA_OUTPUT', '/root/pcsx-state-recovery-20261021'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--disc', type=Path, default=Q / 'card/roms/psx/Earthworm2.cue')
parser.add_argument('--menu', action='store_true')
parser.add_argument('--library-root', type=Path, help='override only privately rebuilt bundled runtime libraries')
parser.add_argument('--core-archive', type=Path, default=Path('/root/lilsmoke-psx-20261010/native/libpcsx4all_rkgame.a'))
parser.add_argument('--cases', nargs='+', default=['baseline', 'baseline-enosys', 'normal', 'sync-enosys', 'all-sync-enosys', 'io-error', 'rename-error', 'enospc', 'gpu-oom', 'screenshot-oom'])
a = parser.parse_args()
assert a.disc.is_file(), a.disc
R = Q / ('run-' + time.strftime('%Y%m%d-%H%M%S'))
R.mkdir()
shutil.copy2(Q / 'pcsx-recovery-engine', R / 'pcsx-recovery-engine')
if a.menu:
    shutil.copy2(Q / 'pcsx-recovery-menu', R / 'pcsx-recovery-menu')
sys.path.insert(0, str(P / 'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import disk_devices, runtime_libraries

overlay = Newc()
commands = []
for case in a.cases:
    assert case in ('baseline', 'baseline-enosys', 'normal', 'sync-enosys', 'all-sync-enosys', 'io-error', 'rename-error', 'enospc', 'gpu-oom', 'screenshot-oom', 'readonly')
    if case == 'readonly':
        shutil.copy2(Q / 'pcsx-readonly-test', R / 'pcsx-readonly-test')
        commands.extend([
            'timeout 30 /bench/pcsx-readonly-test > /bench/readonly.log 2>&1',
            'echo $? > /bench/readonly.exit',
            'cat /bench/readonly.log',
        ])
        continue
    commands.extend([
        f'mkdir -p /bench/states-{case}',
        f'timeout 180 /bench/pcsx-recovery-engine /disc/"$DISC" /bench/states-{case} {case} > /bench/{case}.log 2>&1',
        f'echo $? > /bench/{case}.exit',
        f'cat /bench/{case}.log',
    ])
if a.menu:
    commands.extend(['export SDL_VIDEODRIVER=directfb',
                     'export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,system=dummy,depth=32,pixelformat=ARGB'])
    for mode in ('surface', 'renderer'):
        commands.extend([
            f'timeout 180 /bench/pcsx-recovery-menu /disc/"$DISC" {mode} > /bench/menu-{mode}.log 2>&1',
            f'echo $? > /bench/menu-{mode}.exit',
            f'cat /bench/menu-{mode}.log',
        ])
init = '''#!/bin/sh
export PATH=/bin:/usr/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs dev /dev
mkdir -p /bench /disc /dev/shm /run /persist/config/pcsx4all /persist/bios /persist/saves/pcsx4all /persist/states/pcsx4all /tmp
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o ro,trans=virtio,version=9p2000.L disc /disc || poweroff -f
export SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy HOME=/persist
cat /proc/meminfo > /bench/memory.txt
''' + '\n'.join(commands) + '\nsync\npoweroff -f\n'
overlay.file('init', init, 0o755)
overlay.file('persist/config/pcsx4all/pcsx4all.cfg', (P / 'package/pcsx4all/pcsx4all.cfg').read_bytes())
for name in ('bin/busybox', 'lib/libc.so', 'lib/ld-musl-mipsel.so.1'):
    overlay.file(name, (B / 'target' / name).read_bytes(), 0o755)
for name in subprocess.check_output(['busybox', '--list'], text=True).splitlines():
    overlay.symlink('bin/' + name, 'busybox')
runtime_libraries(B, overlay, [Q / 'pcsx-recovery-engine'])
if a.menu:
    modules = list((B / 'target/usr/lib/directfb-1.7-7').rglob('*.so'))
    for module in modules:
        overlay.file(str(module.relative_to(B / 'target')), module.read_bytes(), 0o755)
    runtime_libraries(B, overlay, [Q / 'pcsx-recovery-menu', *modules])
    overlay.file('usr/share/fonts/dejavu/DejaVuSans.ttf', (B / 'target/usr/share/fonts/dejavu/DejaVuSans.ttf').read_bytes())
if a.library_root:
    for name in ('libSDL2-2.0.so.0', 'libSDL2_ttf-2.0.so.0', 'libfreetype.so.6', 'libpng16.so.16', 'libz.so.1'):
        path = 'usr/lib/' + name
        overlay.entries = [entry for entry in overlay.entries if entry.name != path]
        overlay.file(path, (a.library_root / name).read_bytes(), 0o755)
root = Newc()
parents = {str(parent) for entry in overlay.entries for parent in Path(entry.name).parents if str(parent) != '.'}
parents.update(('dev', 'proc', 'sys', 'tmp', 'run', 'persist', 'disc'))
for name in sorted(parents, key=lambda name: (name.count('/'), name)):
    root.dir(name)
root.chrdev('dev/console', 5, 1)
root.entries += overlay.entries
# Keep filenames out of shell interpolation, including optional user-supplied discs.
init = init.replace('export PATH=', 'DISC=' + "'" + a.disc.name.replace("'", "'\\''") + "'\nexport PATH=", 1)
# Rebuild init through Newc instead of relying on its entry representation.
root.entries = [entry for entry in root.entries if entry.name != 'init']
root.file('init', init, 0o755)
(R / 'pcsx-recovery-boot.cpio.gz').write_bytes(gzip.compress(root.build()))
command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
           '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120',
           '-kernel', str(B / 'images/vmlinux'), '-initrd', str(R / 'pcsx-recovery-boot.cpio.gz'),
           '-append', 'console=ttyS0 panic=-1', '-vga', 'none', '-display', 'none',
           '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
           *disk_devices(B, [('bench', R, False), ('disc', a.disc.parent, True)])]
print('Recovery evidence:', R, flush=True)
with (R / 'pcsx-recovery-serial.log').open('wb') as log:
    vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 190 * (len(a.cases) + (2 if a.menu else 0)) + 30
        while vm.poll() is None and 'reboot: System halted' not in (R / 'pcsx-recovery-serial.log').read_text(errors='replace'):
            if time.monotonic() > deadline:
                raise RuntimeError('recovery VM timeout')
            time.sleep(.5)
    finally:
        if vm.poll() is None:
            vm.terminate()
        try:
            vm.wait(timeout=10)
        except subprocess.TimeoutExpired:
            vm.kill()
            vm.wait(timeout=10)
for case in a.cases:
    log = (R / (case + '.log')).read_text(errors='replace')
    print(log[-500:])
    assert (R / (case + '.exit')).read_text().strip() == '0', case
    assert 'PCSX_RECOVERY_PASS scenario=' + case in log, case
if a.menu:
    for mode in ('surface', 'renderer'):
        log = (R / f'menu-{mode}.log').read_text(errors='replace')
        print(log[-500:])
        assert (R / f'menu-{mode}.exit').read_text().strip() == '0', mode
        assert 'PCSX_RECOVERY_MENU_PASS mode=' + mode in log, mode
def sha256(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()

report = dict(passed=True, cases=a.cases, production_menu=a.menu, evidence=str(R),
              scope='filesystem guards only; serializer not called' if a.cases == ['readonly'] else 'real-disc engine state QA',
              kernel=str(B / 'images/vmlinux'), kernel_sha256=sha256(B / 'images/vmlinux'),
              memory_mib=120, data_limit_mib=80, cpu='74Kf-lilsmoke', cpus=1,
              syscall_faults='guest-kernel seccomp', physical_vendor_kernel_tested=False,
              disc=str(a.disc), disc_share_readonly=True, disc_descriptor_sha256=sha256(a.disc),
              engine_test_sha256=sha256(R / 'pcsx-recovery-engine'),
              core_archive_sha256=sha256(a.core_archive),
              bundled_libraries={f.name:sha256(f) for f in a.library_root.glob('*.so.*') if f.is_file()} if a.library_root else {})
(R / 'pcsx-recovery-results.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS:', report['scope'] + ':', ', '.join(a.cases), R)
