#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Private cached-game handoff test. No card writes or physical FPS claim."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent
P=HERE.parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-vm',type=Path,required=True)
    parser.add_argument('--bootstrap',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,help='Final staged runtime, including the bundled SDL library')
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    previous=json.loads((args.base_vm/'report.json').read_text())
    if previous.get('passed') is not True or not previous.get('filesystem_clean'):
        raise ValueError('A completed, clean private VM fixture is required')
    out=args.out.resolve()
    out.mkdir(exist_ok=False)
    disk=out/'PRIVATE-game.fat'
    subprocess.run(['cp','--sparse=always',str(args.base_vm/'PRIVATE-application.fat'),str(disk)],check=True)
    subprocess.run(['/usr/sbin/fsck.vfat','-n',str(disk)],check=True,capture_output=True)
    replacements=[(args.bootstrap,'bin/sm64-prepare'),(HERE/'native_build.py','native_build.py')]
    if args.runtime:
        replacements += [(args.runtime/'lib/libSDL2-2.0.so.0','lib/libSDL2-2.0.so.0')]
    for source,destination in replacements:
        subprocess.run(['mcopy','-o','-i',str(disk),str(source),'::/Test App/runtime/'+destination],check=True)
    preload=out/'qa-mmio.so'
    subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-shared','-fPIC','-O2',
        '-Wall','-Wextra','-Werror',str(HERE/'qa_mmio_budget.c'),'-o',str(preload)],check=True)
    sys.path.insert(0,str(P/'tools/hwtest'))
    sys.path.insert(0,str(P/'tools/portsmoke/sm64'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs,runtime_libraries
    from vm_scene_smoke import Qmp
    init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /portdisk /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t vfat -o rw,umask=000 /dev/sdb /portdisk || poweroff -f
mount -t tmpfs -o size=8m tmpfs /dev/shm
date -s '1970-01-01 08:00:00'
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,depth=24,pixelformat=RGB24
export LILSMOKE_APP_DIR='/portdisk/Test App' LILSMOKE_APP_ID=org.lilsmoke.sm64
export LILSMOKE_PRIVATE_HARDWARE_QA=1 LILSMOKE_DFB_EVDEV_KEYCODES=1
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export LILSMOKE_SESSION_LOG=/usr/bin/qa-session-log
export SM64_ORIGINAL_INPUT_TRACE=1 SM64_ORIGINAL_RENDER_EVERY=1
cat /proc/meminfo
for budget in 0 24; do
    echo QA_CACHED_BEGIN=$budget
    export LILSMOKE_HCFB=1 LILSMOKE_QA_MMIO_MIB=$budget
    export SM64_ORIGINAL_TEST_FRAMES=900 SM64_ORIGINAL_INTERACTIVE_AFTER=600
    [ "$budget" = 0 ] || export SM64_ORIGINAL_TEST_FRAMES=30
    /bin/sh '/portdisk/Test App/SM64.sh'
    echo QA_CACHED_EXIT=$?
done
sync
umount /portdisk
echo QA_STORAGE_UMOUNT=$?
poweroff -f
'''
    overlay=Newc()
    overlay.file('usr/bin/bench-init',init,0o755)
    overlay.file('usr/bin/qa-session-log','#!/bin/sh\nprintf "%s\\n" /dev/console\n',0o755)
    overlay.file('os/libm8-display.so',preload.read_bytes(),0o755)
    overlay.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
    overlay.file('etc/lilsmoke/input-maps/default.map',(P/'board/qemu-mipsel/input-maps/default.map').read_bytes())
    runtime_libraries(B,overlay,[args.bootstrap])
    initrd=out/'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(B,overlay,'/usr/bin/bench-init',
        overlay_mib=16,root_device='/dev/sda',root_fstype='ext2')))
    command=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
        '-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','120','-rtc','base=1970-01-01T00:00:00',
        '-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),
        '-append','console=ttyS0 video=cirrusfb:640x480M-16@60 panic=-1',
        '-vga','cirrus','-display','none','-serial','stdio','-monitor','none','-no-reboot','-device','usb-kbd',
        '-qmp',f'unix:{out}/qmp.sock,server=on,wait=off',
        '-drive',f'file={args.base_vm}/root.ext2,if=ide,index=0,media=disk,format=raw,snapshot=on',
        '-drive',f'file={disk},if=ide,index=1,media=disk,format=raw']
    def read():
        return (out/'serial.log').read_text(errors='replace')
    qmp=None
    sent=False
    opened=None
    resumed=False
    with (out/'serial.log').open('wb') as serial:
        vm=subprocess.Popen(command,stdout=serial,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL)
        try:
            deadline=time.monotonic()+900
            while vm.poll() is None and 'reboot: System halted' not in read():
                text=read()
                if qmp is None and (out/'qmp.sock').exists(): qmp=Qmp(out/'qmp.sock')
                if qmp and not sent and 'OriginalFrame=600 ' in text:
                    qmp.call('screendump',dict(filename=str(out/'game.ppm')))
                    qmp.call('send-key',dict(keys=[dict(type='qcode',data=x) for x in ('ret','spc')],**{'hold-time':1500}))
                    sent=True
                if qmp and sent and opened is None and 'SM64_MENU opened' in text:
                    qmp.call('screendump',dict(filename=str(out/'menu.ppm')))
                    opened=time.monotonic()
                if qmp and opened is not None and not resumed and time.monotonic()-opened>1.6:
                    qmp.call('send-key',dict(keys=[dict(type='qcode',data='alt')],**{'hold-time':1000}))
                    resumed=True
                if time.monotonic()>deadline: raise RuntimeError('Cached handoff VM timed out')
                time.sleep(.5)
        finally:
            if qmp: qmp.close()
            if vm.poll() is None: vm.terminate()
            try: vm.wait(timeout=10)
            except subprocess.TimeoutExpired: vm.kill();vm.wait()
    text=read()
    assert text.count('QA_CACHED_EXIT=0')==2,text[-4000:]
    assert text.count('SM64_HANDOFF cached=1 preparation_skipped=1')==2
    assert text.count('SM64_GAME exit=0 signal=0 cached_hash_verified=1')==2
    assert 'SM64_STAGE compile_begin' not in text and 'SM64_STAGE unpack_begin' not in text
    assert 'OriginalEngineTest=FINISHED Frames=900 Rendered=900' in text
    assert 'OriginalEngineTest=FINISHED Frames=30 Rendered=30' in text
    assert 'SM64_MENU resumed' in text and resumed
    assert 'QA_MMIO mapped_bytes=25165824 physical_pages_untouched=1' in text
    assert 'QA_STORAGE_UMOUNT=0' in text
    assert not re.search(r'Out of memory|oom-kill|Killed process|virtual memory exhausted',text,re.I)
    assert int(re.search(r'SwapTotal:\s+(\d+)',text).group(1))==0
    checked=subprocess.run(['/usr/sbin/fsck.vfat','-n',str(disk)],capture_output=True,text=True)
    (out/'filesystem.log').write_text(checked.stdout+checked.stderr)
    assert checked.returncode==0
    from PIL import Image
    for path in out.glob('*.ppm'):
        with Image.open(path) as picture: picture.save(path.with_suffix('.png'))
    result=dict(passed=True,cached_without_compilation=True,game_frames=900,menu_and_resume=True,
        repeat_launch=True,mmio_virtual_mib=24,cpus=1,physical_ram_mib=120,swap_kib=0,
        game_virtual_limit_kib=98304,filesystem_clean=True,physical_hc16xx=False,
        bootstrap_sha256=hashlib.sha256(args.bootstrap.read_bytes()).hexdigest(),
        backend_sha256=hashlib.sha256((HERE/'native_build.py').read_bytes()).hexdigest())
    if args.runtime:
        result['bundled_sdl_sha256']=hashlib.sha256((args.runtime/'lib/libSDL2-2.0.so.0').read_bytes()).hexdigest()
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
