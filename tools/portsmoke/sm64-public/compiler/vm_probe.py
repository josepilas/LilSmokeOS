#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Native compilation in a one-CPU 120 MiB MIPS guest, not a PC cross build."""
import argparse
import gzip
import json
from pathlib import Path
import re
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
P = HERE.parents[3]
B = Path('/root/lilsmoke-os/build/buildroot/output')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
Q = args.out
Q.mkdir(exist_ok=False)
sys.path.insert(0, str(P / 'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs, disk_devices

init = r'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /sdk /project /compiler /source /dev/pts
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,ro sdk /sdk || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,ro project /project || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,ro compiler /compiler || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,ro source /source || poweroff -f
cat /proc/meminfo > /bench/memory.txt
(
    while :; do
        for path in /proc/[0-9]*/status; do
            grep -E 'Name:|VmPeak:|VmRSS:' "$path" 2>/dev/null
        done
        sleep 1
    done
) > /bench/process.log 2>&1 &
ulimit -v 92160
/compiler/wrappers/cc -O2 -mips32r2 -mabi=32 -mhard-float -mfp32 -mdspr2 \
    -nostdinc -isystem /compiler/usr/lib/gcc/mipsel-linux-gnu/12/include -isystem /sdk/usr/include \
    -c /project/tools/portsmoke/sm64-public/compiler/probe.c -o /bench/probe.o > /bench/compile.log 2>&1
code=$?
echo QA_NATIVE_COMPILE=$code
if [ "$code" = 0 ]; then
    /compiler/wrappers/ld -m elf32ltsmip --dynamic-linker /lib/ld-musl-mipsel.so.1 \
        -o /bench/probe /sdk/lib/crt1.o /sdk/lib/crti.o /bench/probe.o \
        -L /sdk/usr/lib -L /sdk/lib -lc /sdk/lib/crtn.o >> /bench/compile.log 2>&1
    echo QA_NATIVE_LINK=$?
    /bench/probe
    echo QA_NATIVE_RUN=$?
    cd /source
    /compiler/wrappers/cc -O3 -mips32r2 -mabi=32 -mhard-float -mfp32 -mdspr2 \
        -fno-fast-math -fno-strict-aliasing -fwrapv -fcommon -fno-math-errno \
        -D_LANGUAGE_C -DVERSION_US -DNOWUP -DNON_MATCHING -DAVOID_UB -DTARGET_LINUX \
        -DNO_SEGMENTED_MEMORY -DENABLE_SOFTRAST -DF3DEX_GBI_2E -DCONVERT \
        -nostdinc -isystem /compiler/usr/lib/gcc/mipsel-linux-gnu/12/include \
        -isystem /sdk/usr/include -I/sdk/usr/include/SDL2 -Iinclude -Isrc -I. \
        -Ibuild/original-mips/include -Ibuild/original-mips \
        -c src/pc/gfx/gfx_soft.c -o /bench/private-gfx-probe.o > /bench/graphics-compile.log 2>&1
    echo QA_NATIVE_GRAPHICS_COMPILE=$?
fi
sync
poweroff -f
'''
overlay = Newc()
overlay.file('usr/bin/bench-init', init, 0o755)
initrd = Q / 'boot.cpio.gz'
initrd.write_bytes(gzip.compress(overlay_initramfs(B, overlay,
    '/usr/bin/bench-init', overlay_mib=16, rootfs_in_ram=True)))
qemu = '/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel'
command = [qemu, '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120M',
    '-kernel', str(B / 'images/vmlinux'), '-initrd', str(initrd),
    '-append', 'console=ttyS0 rdinit=/init panic=1', '-display', 'none',
    '-monitor', 'none', '-serial', 'file:' + str(Q / 'serial.log'), '-no-reboot']
command += disk_devices(B, [('bench', Q, False), ('sdk', B / 'staging', True),
    ('project', P, True), ('compiler', Path('/root/lilsmoke-public-compiler/runtime'), True),
    ('source', Path('/root/lilsmoke-sm64-original-private-iig2ryae'), True)])
started = time.monotonic()
with (Q / 'qemu.log').open('w') as output:
    process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
    try:
        deadline = started + 600
        while process.poll() is None:
            serial = (Q / 'serial.log').read_text(errors='replace') if (Q / 'serial.log').exists() else ''
            if 'reboot: System halted' in serial:
                break
            if time.monotonic() > deadline:
                raise RuntimeError('Native compiler VM exceeded ten minutes')
            time.sleep(1)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
serial = (Q / 'serial.log').read_text(errors='replace')
memory = (Q / 'memory.txt').read_text()
sample = (Q / 'process.log').read_text()
passed = all(text in serial for text in ('QA_NATIVE_COMPILE=0', 'QA_NATIVE_LINK=0',
    'PASS_NATIVE_MIPS_MUSL_COMPILER', 'QA_NATIVE_RUN=0', 'QA_NATIVE_GRAPHICS_COMPILE=0'))
result = dict(passed=passed, guest_cpu='74Kf-lilsmoke', guest_cpus=1,
    guest_ram_mib=120, process_virtual_limit_kib=92160,
    native_mips_compiler=True, game_uses_musl=True, software_graphics_compiled_locally=True,
    public_gameplay_tested=False, public_game_package_ready=False,
    guest_memtotal_kib=int(re.search(r'MemTotal:\s+(\d+)', memory).group(1)),
    max_sampled_process_rss_kib=max(map(int, re.findall(r'VmRSS:\s+(\d+)', sample)), default=0),
    elapsed_seconds=round(time.monotonic()-started, 2))
(Q / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
if not passed:
    print(serial[-4000:])
    raise SystemExit(1)
