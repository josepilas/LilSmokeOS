#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Private owner-data test in a real 128 MiB Linux/MIPS guest, 112 MiB app AS.

Uses retained read-only SDK/rootfs and owner exports; no SD writes or owner copies.
--cache-fs fat32 uses one temporary sparse Linux-hosted virtio disk, not 9p.
"""
import argparse
import errno
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time

HERE=Path(__file__).resolve().parent
PROJECT=HERE.parents[1]
SDK=Path(os.environ.get('SDK','/root/lilsmoke-os/build/buildroot/output'))
sys.path.insert(0,str(PROJECT/'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda:source.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def output_bytes(directory):
    total=0
    for path in directory.rglob('*'):
        try:
            info=path.stat()
        except FileNotFoundError:
            continue
        if stat.S_ISREG(info.st_mode):total+=info.st_size
    return total


def owner_snapshot(wad):
    files=[wad]+sorted((p for p in wad.parent.iterdir() if p.is_file() and p.suffix.lower()=='.ogg'),key=lambda p:p.name)
    records={p.name:digest(p) for p in files}
    return dict(files=len(records),sha256=hashlib.sha256(json.dumps(records,sort_keys=True).encode()).hexdigest())


def active_serial(path,report):
    try:return path.read_text(errors='replace')
    except OSError as error:
        # DrvFs can return ENODATA while QEMU is appending. Closed logs must
        # still be read and validated normally after the guest has stopped.
        if error.errno!=errno.ENODATA:raise
        report['serial_poll_retries']=report.get('serial_poll_retries',0)+1
        return ''


def guest_init(wad_name,frames,audio,cache_fs):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+',wad_name):
        raise ValueError('Unsupported owner WAD filename')
    init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
fail() {
  echo "UNDERTALE_VM_FAILED $*"
  sync
  poweroff -f
  exit 1
}
mkdir -p /bench /original /program /cache-disk /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=2m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || fail bench-mount
mount -t 9p -o trans=virtio,version=9p2000.L,ro owner /original || fail owner-mount
mount -t 9p -o trans=virtio,version=9p2000.L,ro program /program || fail program-mount
CACHE_MOUNT
export LD_LIBRARY_PATH=/program/lib FROGGY_INPUT_DIR=/nonexistent FROGGY_NO_SHARED_INPUT=1
export FROGGY_NONINTERACTIVE=1 FROGGY_CACHE_DIR=CACHE_ROOT/cache FROGGY_SAVE_DIR=CACHE_ROOT/saves
export SDL_AUDIODRIVER=dummy FROGGY_AUDIO_MANUAL=0
AUDIO_ENV
cd /bench
cat /proc/meminfo > /bench/memory.txt
cat /proc/mounts > /bench/mounts.txt
run_case() {
  name=$1
  shift
  cat /proc/uptime >"/bench/$name-start.txt"
  (
    ulimit -c 0
    ulimit -v 114688 || exit 70
    echo ADDRESS_LIMIT_KIB=114688
    exec /program/app "$@"
  ) >"/bench/$name.log" 2>&1 &
  pid=$!
  samples=0
  while kill -0 "$pid" 2>/dev/null; do
    cat "/proc/$pid/status" >>"/bench/$name-memory.log" 2>/dev/null
    cat "/proc/$pid/limits" >"/bench/$name-limits.txt" 2>/dev/null
    if [ "$((samples % 50))" -eq 0 ]; then
      for file in status wchan syscall stack; do
        echo "=== $file ==="
        cat "/proc/$pid/$file" 2>/dev/null
      done >"/bench/$name-state.txt"
    fi
    samples=$((samples+1))
    sleep 0.1
  done
  wait "$pid"
  code=$?
  cat /proc/uptime >"/bench/$name-end.txt"
  echo "$code" >"/bench/$name.exit"
  echo "UNDERTALE_VM_CASE $name exit=$code"
  return "$code"
}
run_case first --prepare-only /original/WAD --headless || fail first
ls -laR "$FROGGY_CACHE_DIR" > /bench/cache-first-inventory.txt
find "$FROGGY_CACHE_DIR" -name owner.pid > /bench/cache-first-locks.txt
test ! -s /bench/cache-first-locks.txt || fail first-retained-preparation-lock
echo UNDERTALE_VM_FIRST_COMPLETE_UNLOCKED
CACHE_REMOUNT
run_case cached --prepare-only /original/WAD --headless || fail cached
run_case runtime /original/WAD --headless --exit-at-frame FRAMES --trace-frames || fail runtime
ls -laR "$FROGGY_CACHE_DIR" "$FROGGY_SAVE_DIR" > /bench/cache-inventory.txt
find "$FROGGY_CACHE_DIR" -name owner.pid > /bench/cache-locks.txt
test ! -s /bench/cache-locks.txt || fail retained-preparation-lock
CACHE_UNMOUNT
echo UNDERTALE_VM_DONE
sync
poweroff -f
'''.replace('WAD',wad_name).replace('FRAMES',str(frames)).replace('AUDIO_ENV',
    'export FROGGY_HEADLESS_AUDIO=1' if audio else 'unset FROGGY_HEADLESS_AUDIO')
    mount='mount -t vfat -o rw,nosuid,nodev,noexec,umask=0077 /dev/vdb /cache-disk || fail fat32-mount'
    remount='''sync
umount /cache-disk || fail fat32-unmount-first
CACHE_MOUNT_COMMAND
cat /proc/mounts > /bench/mounts-cached.txt
echo UNDERTALE_VM_FAT32_REMOUNTED
'''.replace('CACHE_MOUNT_COMMAND',mount)
    for key,value in dict(CACHE_MOUNT=mount if cache_fs=='fat32' else '',
        CACHE_ROOT='/cache-disk' if cache_fs=='fat32' else '/bench',
        CACHE_REMOUNT=remount if cache_fs=='fat32' else '',
        CACHE_UNMOUNT='sync\numount /cache-disk || fail fat32-unmount-final' if cache_fs=='fat32' else '').items():
        init=init.replace(key,value)
    return init


def create_cache_disk(directory):
    directory=Path(directory)
    fs=subprocess.check_output(['findmnt','-n','-o','FSTYPE','-T',str(directory)],text=True).strip()
    if fs not in ('ext2','ext3','ext4','xfs','btrfs'):
        raise RuntimeError('FAT32 scratch must be on a Linux disk filesystem, not '+fs)
    image=directory/'cache-fat32.img'
    with image.open('xb') as f:f.truncate(256*1024*1024)
    subprocess.run(['mkfs.fat','-F','32','-n','FTCACHE',str(image)],check=True,capture_output=True)
    return image,fs


def collect_cases(out):
    cases=[]
    for name in ('first','cached','runtime'):
        exit_file=out/(name+'.exit')
        if not exit_file.exists():continue
        memory=out/(name+'-memory.log')
        text=memory.read_text(errors='replace') if memory.exists() else ''
        values=lambda key:[int(x) for x in re.findall(r'^'+key+r':\s+(\d+) kB',text,re.M)]
        elapsed=float((out/(name+'-end.txt')).read_text().split()[0])-float((out/(name+'-start.txt')).read_text().split()[0])
        cases.append(dict(name=name,exit_code=int(exit_file.read_text().strip()),
            elapsed_seconds=round(elapsed,2),peak_as_kib=max(values('VmPeak'),default=0),
            peak_rss_kib=max(values('VmHWM'),default=0)))
    return cases


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--wad',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--frames',type=int,default=600)
    p.add_argument('--audio',action='store_true',help='Enable the real SDL callback (automatic for FAT32)')
    p.add_argument('--cache-fs',choices=('9p','fat32'),default='9p')
    p.add_argument('--scratch-root',type=Path,default=Path('/tmp'),help='Existing Linux disk directory for the temporary FAT32 image')
    p.add_argument('--timeout',type=int,default=1500)
    a=p.parse_args()
    if a.frames<=0 or a.timeout<=0:p.error('frames and timeout must be positive')
    a.wad=a.wad.resolve(strict=True);a.out=a.out.resolve()
    if a.cache_fs=='fat32':a.audio=True
    init=guest_init(a.wad.name,a.frames,a.audio,a.cache_fs)
    a.out.mkdir(parents=True,exist_ok=False)
    owner_before=digest(a.wad)
    owner_files_before=owner_snapshot(a.wad)
    binary=HERE/'_build/mips/froggytale-lilsmoke'
    program=a.out/'program'
    (program/'lib').mkdir(parents=True)
    shutil.copyfile(binary,program/'app')
    (program/'app').chmod(0o755)
    shutil.copyfile((SDK/'target/usr/lib/libSDL2-2.0.so.0').resolve(),program/'lib/libSDL2-2.0.so.0')
    cc=SDK/'host/bin/mipsel-buildroot-linux-musl-gcc'
    atomic=Path(subprocess.check_output([str(cc),'-print-file-name=libatomic.so.1'],text=True).strip())
    shutil.copyfile(atomic.resolve(strict=True),program/'lib/libatomic.so.1')
    overlay=Newc()
    overlay.file('usr/bin/bench-init',init,0o755)
    initrd=a.out/'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(SDK,overlay,'/usr/bin/bench-init',overlay_mib=16,trace=True)))
    devices=disk_devices(SDK,[('bench',a.out,False),('owner',a.wad.parent,True),('program',program,True)])
    root='/root/lilsmoke-media-c45-final-video/rootfs.squashfs'
    devices=[item.replace(str(SDK/'images/rootfs.squashfs'),root) for item in devices]
    command=['taskset','-c',os.environ.get('FROGGY_CPU','1'),'nice','-n','15',
        '/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta',
        '-cpu','74Kf','-smp','1','-m','128','-kernel',str(SDK/'images/vmlinux'),
        '-initrd',str(initrd),'-append','console=ttyS0 panic=-1','-display','none',
        '-serial','stdio','-monitor','none','-no-reboot',*devices]
    report=dict(passed=False,ram_mib=128,app_address_limit_mib=112,cpu='74Kf',cpus=1,
        gpu_3d=False,physical_hardware=False,owner_export_read_only=True,
        program_export_read_only=True,cache_filesystem=a.cache_fs,sd_touched=False,
        runtime_requested_frames=a.frames,
        audio_requested=a.audio,physical_audio_tested=False,
        binary_sha256=digest(program/'app'),owner_sha256=owner_before,
        owner_files_before=owner_files_before,
        libatomic_sha256=digest(program/'lib/libatomic.so.1'),output_limit_bytes=256*1024*1024,cases=[])
    start=time.monotonic()
    scratch=None
    try:
        if a.cache_fs=='fat32':
            scratch=tempfile.TemporaryDirectory(prefix='froggytale-fat32-',dir=a.scratch_root.resolve(strict=True))
            image,fs=create_cache_disk(scratch.name)
            report.update(cache_disk_logical_bytes=image.stat().st_size,cache_disk_host_filesystem=fs,
                cache_disk_temporary=True,cache_disk_retained=False)
            command+=['-drive',f'file={image},if=none,id=ftcache,format=raw',
                '-device','virtio-blk-pci,drive=ftcache,addr=0x14.4']
        (a.out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
        with (a.out/'serial.log').open('w') as log:
            proc=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
            try:
                while True:
                    try:proc.wait(timeout=5);break
                    except subprocess.TimeoutExpired:
                        size=output_bytes(a.out)
                        if size>report['output_limit_bytes']:raise RuntimeError('VM output budget exceeded')
                        if time.monotonic()-start>a.timeout:raise TimeoutError(f'VM exceeded {a.timeout} seconds')
                        serial=active_serial(a.out/'serial.log',report)
                        if 'System halted' in serial or 'Power down' in serial:
                            report['guest_shutdown_observed']=True
                            proc.terminate()
                            proc.wait(timeout=10)
                            break
            except BaseException:
                proc.terminate()
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:proc.kill();proc.wait()
                raise
        report['vm_return_code']=proc.returncode
        assert proc.returncode==0 or (report.get('guest_shutdown_observed') and proc.returncode==-signal.SIGTERM), 'QEMU returned an error'
        report['cases']=collect_cases(a.out)
        assert 'UNDERTALE_VM_DONE' in (a.out/'serial.log').read_text(errors='replace')
        assert len(report['cases'])==3 and all(c['exit_code']==0 for c in report['cases'])
        assert all(0<c['peak_as_kib']<=112*1024 and 0<c['peak_rss_kib']<=128*1024 for c in report['cases'])
        assert 'validation=sealed-metadata' in (a.out/'cached.log').read_text()
        assert 'FROGGY_EXIT status=0' in (a.out/'runtime.log').read_text()
        if a.audio:
            text=(a.out/'runtime.log').read_text()
            assert 'manual=0' in text, 'SDL callbacks were suppressed'
            match=re.search(r'UNDERTALE_AUDIO ready=1 frames=(\d+) nonzero_frames=(\d+)',text)
            assert match and int(match[1])>0, 'SDL audio did not pull frames'
            assert int(match[2])>0, 'The runtime mixed only silence'
            report['audio_frames']=int(match[1])
            report['nonzero_pcm_frames']=int(match[2])
        assert 'cache=created' in (a.out/'first.log').read_text()
        assert 'UNDERTALE_VM_FIRST_COMPLETE_UNLOCKED' in (a.out/'serial.log').read_text()
        assert not (a.out/'cache-locks.txt').read_text().strip(), 'Preparation lock was retained'
        mounts=(a.out/'mounts.txt').read_text().splitlines()
        for tag,target in (('owner','/original'),('program','/program')):
            assert any(line.split()[:3]==[tag,target,'9p'] and 'ro' in line.split()[3].split(',') for line in mounts)
        if a.cache_fs=='fat32':
            for filename in ('mounts.txt','mounts-cached.txt'):
                assert any(line.split()[:3]==['/dev/vdb','/cache-disk','vfat'] and 'rw' in line.split()[3].split(',')
                    for line in (a.out/filename).read_text().splitlines()), 'Cache was not on the FAT32 block disk'
            assert 'UNDERTALE_VM_FAT32_REMOUNTED' in (a.out/'serial.log').read_text()
            assert not (a.out/'cache').exists() and not (a.out/'saves').exists(), 'Cache/save escaped to 9p'
            report['cache_remount_verified']=True
            report['cache_disk_allocated_bytes']=image.stat().st_blocks*512
        assert owner_snapshot(a.wad)==owner_files_before, 'Owner WAD/OGGs changed'
        report['passed']=True
    except BaseException as error:
        report['error']=f'{type(error).__name__}: {error}'
        raise
    finally:
        if scratch is not None:scratch.cleanup()
        report['owner_original_unchanged']=digest(a.wad)==owner_before
        report['owner_wad_and_oggs_unchanged']=owner_snapshot(a.wad)==owner_files_before
        report['cases']=collect_cases(a.out)
        report['elapsed_seconds']=time.monotonic()-start
        report['output_bytes']=output_bytes(a.out)
        (a.out/'PRIVATE-report.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':main()
