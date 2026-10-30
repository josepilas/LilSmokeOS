#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Extraction screen component QA only; this is not a gameplay acceptance test."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from PIL import Image

HERE=Path(__file__).resolve().parent
P=HERE.parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
args=parser.parse_args()
Q=args.out
Q.mkdir(exist_ok=False)
app=Q/'Test App'
app.mkdir()
shutil.copyfile(P/'package/portsmoke-sm64/src/icon.webp', app/'icon.webp')
binary=Path('/root/lilsmoke-public-bootstrap/sm64-prepare')
sys.path.insert(0,str(P/'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs, disk_devices, runtime_libraries
init=r'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t tmpfs -o size=8m tmpfs /dev/shm
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,depth=24,pixelformat=RGB24
ulimit -v 65536
/usr/bin/sm64-prepare '/bench/Test App' --ui-test > /bench/ui.log 2>&1
echo QA_EXTRACTION_UI=$?
sync
poweroff -f
'''
overlay=Newc()
overlay.file('usr/bin/bench-init',init,0o755)
overlay.file('usr/bin/sm64-prepare',binary.read_bytes(),0o755)
overlay.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
runtime_libraries(B,overlay,[binary])
initrd=Q/'boot.cpio.gz'
initrd.write_bytes(gzip.compress(overlay_initramfs(B,overlay,'/usr/bin/bench-init',overlay_mib=16,rootfs_in_ram=True)))
command=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
    '-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','120',
    '-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),
    '-append','console=ttyS0 video=cirrusfb:640x480M-16@60 panic=-1',
    '-vga','cirrus','-display','none','-serial','stdio','-monitor','none','-no-reboot',
    *disk_devices(B,[('bench',Q,False)])]
started=time.monotonic()
with (Q/'serial.log').open('wb') as log:
    vm=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    try:
        while vm.poll() is None:
            if 'reboot: System halted' in (Q/'serial.log').read_text(errors='replace'):
                break
            if time.monotonic()-started>120:
                raise RuntimeError('UI component VM timed out')
            time.sleep(0.5)
    finally:
        if vm.poll() is None: vm.terminate()
        try: vm.wait(timeout=5)
        except subprocess.TimeoutExpired: vm.kill(); vm.wait()
serial=(Q/'serial.log').read_text(errors='replace')
with Image.open(app/'extraction-ui-test.bmp') as source:
    source=source.convert('RGB')
    assert source.size==(640,360)
    pixels=list(source.getdata())
    blue=sum(1 for r,g,b in pixels if b>g+35 and g>r+30)
    dark=sum(1 for r,g,b in pixels if max(r,g,b)<140)
    assert blue>4000 and dark>1500, (blue,dark)
    source.save(Q/'extraction-ui-mips.png')
result=dict(passed='QA_EXTRACTION_UI=0' in serial, native_mips=True,
    guest_cpu='74Kf-lilsmoke', guest_cpus=1, guest_ram_mib=120,
    process_virtual_limit_kib=65536, canvas=[640,360], blue_bar_pixels=blue,
    dark_text_pixels=dark, synthetic_progress_fixture=True,
    real_rom_extraction_tested=False, game_launch_tested=False,
    screenshot='extraction-ui-mips.png')
(Q/'report.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
if not result['passed']: raise SystemExit(1)
