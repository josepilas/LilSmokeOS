#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Install the actual asset-free release archive with the production MIPS helper."""
import argparse
import gzip
import json
import shutil
from pathlib import Path
import subprocess
import re
import sys
import time

HERE=Path(__file__).resolve().parent
P=HERE.parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--package',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--timeout',type=int,default=600)
args=parser.parse_args()
if not 60 <= args.timeout <= 7200: raise ValueError('Invalid VM time budget')
Q=args.out
Q.mkdir(exist_ok=False)
(Q/'storage').mkdir()
payload=Q/'package-input'
payload.mkdir()
shutil.copyfile(args.package,payload/args.package.name)
package_disk=Q/'package.ext4'
with package_disk.open('xb') as handle: handle.truncate(512*1024*1024)
subprocess.run(['mke2fs','-q','-F','-t','ext4','-O','^orphan_file,^metadata_csum_seed',
    '-d',str(payload),str(package_disk)],check=True)
storage_disk=Q/'storage.ext4'
with storage_disk.open('xb') as handle: handle.truncate(1024*1024*1024)
subprocess.run(['mke2fs','-q','-F','-t','ext4','-O','^orphan_file,^metadata_csum_seed',
    str(storage_disk)],check=True)
sys.path.insert(0,str(P/'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
assert re.fullmatch(r'[A-Za-z0-9._-]+\.lilpck', args.package.name)
init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /packages /storage /persist
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
count=0
while [ ! -b /dev/vdb ] || [ ! -b /dev/vdc ]; do
    count=$((count+1))
    [ "$count" -lt 30 ] || { echo QA_DISPOSABLE_DISKS_MISSING; poweroff -f; }
    sleep 1
done
mount -t ext4 -o ro /dev/vdb /packages || poweroff -f
mount -t ext4 /dev/vdc /storage || poweroff -f
/usr/lib/lilsmoke/lilsmoke-apps inspect /packages/SM64-LilSmoke-Pre-Alpha-0.0.1.lilpck /bench/icon.webp > /bench/inspect.log 2>&1
echo QA_INSPECT=$?
/usr/lib/lilsmoke/lilsmoke-apps install /packages/SM64-LilSmoke-Pre-Alpha-0.0.1.lilpck > /bench/install.log 2>&1
echo QA_INSTALL=$?
/usr/lib/lilsmoke/lilsmoke-apps list > /bench/list.log 2>&1
sync
umount /storage
echo QA_STORAGE_UMOUNT=$?
poweroff -f
'''
init=init.replace('SM64-LilSmoke-Pre-Alpha-0.0.1.lilpck',args.package.name)
overlay=Newc()
overlay.file('usr/bin/bench-init',init,0o755)
helper=B/'target/usr/lib/lilsmoke/lilsmoke-apps'
overlay.file('usr/lib/lilsmoke/lilsmoke-apps',helper.read_bytes(),0o755)
runtime_libraries(B,overlay,[helper])
initrd=Q/'boot.cpio.gz'
initrd.write_bytes(gzip.compress(overlay_initramfs(B,overlay,'/usr/bin/bench-init',overlay_mib=16)))
command=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
    '-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','120',
    '-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),
    '-append','console=ttyS0 panic=-1','-display','none','-serial','stdio',
    '-monitor','none','-no-reboot',*disk_devices(B,[('bench',Q,False)]),
    '-drive',f'file={package_disk},if=none,id=package,format=raw,readonly=on',
    '-device','virtio-blk-pci,drive=package,addr=0x14.2',
    '-drive',f'file={storage_disk},if=none,id=storage,format=raw',
    '-device','virtio-blk-pci,drive=storage,addr=0x14.3']
with (Q/'serial.log').open('wb') as log:
    vm=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    try:
        deadline=time.monotonic()+args.timeout
        while vm.poll() is None and 'reboot: System halted' not in (Q/'serial.log').read_text(errors='replace'):
            if time.monotonic()>deadline: raise RuntimeError('Package installation VM timed out')
            time.sleep(.5)
    finally:
        if vm.poll() is None: vm.terminate()
        try: vm.wait(timeout=10)
        except subprocess.TimeoutExpired: vm.kill();vm.wait()
serial=(Q/'serial.log').read_text(errors='replace')
assert 'QA_INSPECT=0' in serial and 'QA_INSTALL=0' in serial,serial[-2000:]
assert 'QA_STORAGE_UMOUNT=0' in serial
dump=Q/'installed'
dump.mkdir()
with (Q/'debugfs.log').open('wb') as log:
    subprocess.run(['debugfs','-R','rdump /independent-apps '+str(dump),str(storage_disk)],
        stdout=log,stderr=subprocess.STDOUT,check=True)
app=dump/'independent-apps/Super Mario 64'
assert app.is_dir() and json.loads((app/'meta.json').read_text())['id']=='org.lilsmoke.sm64'
assert [path.name for path in app.glob('*.sh')]==['SM64.sh']
assert not (app/'.sm64-cache').exists()
from verify_installed import verify
result=verify(Q,args.package)
print(json.dumps(result,indent=2))
