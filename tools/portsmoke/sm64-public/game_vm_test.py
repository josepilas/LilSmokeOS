#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Production home -> Applications -> BYO-cache bootstrap -> original game in MIPS Linux.

The private cache was cooked from the owner's ROM separately. This is not a
physical HC16xx FPS measurement or a fresh full-VM compilation claim.
"""
import argparse
import gzip
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from PIL import Image

HERE=Path(__file__).resolve().parent
P=HERE.parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apps',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    Q=args.out.resolve()
    Q.mkdir(exist_ok=False)
    sys.path.insert(0,str(P/'tools/hwtest'))
    sys.path.insert(0,str(P/'tools/portsmoke/sm64'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
    from vm_scene_smoke import Qmp
    init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /storage/independent-apps /persist/config /persist/logs /dev/pts /dev/shm /storage/roms
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L apps /storage/independent-apps || poweroff -f
mount -t tmpfs -o size=8m tmpfs /dev/shm
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,depth=24,pixelformat=RGB24
export LILSMOKE_DFB_EVDEV_KEYCODES=1 LILSMOKE_HCFB=0 LILSMOKE_AUDIO_OVERRIDE=dummy
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24 LILSMOKE_PRIVATE_HARDWARE_QA=1
export SM64_ORIGINAL_TEST_FRAMES=3360 SM64_ORIGINAL_RENDER_EVERY=1 SM64_ORIGINAL_INTERACTIVE_AFTER=3000
export SM64_ORIGINAL_INPUT_TRACE=1 SM64_ORIGINAL_EVIDENCE=/bench
export LILSMOKE_LOG_DIR=/bench
printf 'DISPLAY_WIDTH=856\nDISPLAY_HEIGHT=480\nPOINTER_ENABLED=off\nSCAN_ROMS_ON_BOOT=off\n' > /persist/config/lilsmoke.conf
cat /proc/meminfo > /bench/memory.txt
(
    while :; do
        sleep 5
        for comm in /proc/[0-9]*/comm; do
            read name < "$comm" || continue
            [ "$name" = game ] || continue
            cat "${comm%/comm}/status" 2>/dev/null
        done
    done
) > /bench/process.log 2>&1 &
/usr/bin/lilsmoke-ui > /bench/ui.log 2>&1
echo QA_UI_EXIT=$?
sync
poweroff -f
'''
    overlay=Newc()
    overlay.file('usr/bin/bench-init',init,0o755)
    ui=B/'target/usr/bin/lilsmoke-ui'
    helper=B/'target/usr/lib/lilsmoke/lilsmoke-apps'
    overlay.file('usr/bin/lilsmoke-ui',ui.read_bytes(),0o755)
    overlay.file('usr/lib/lilsmoke/lilsmoke-apps',helper.read_bytes(),0o755)
    overlay.file('usr/lib/lilsmoke/lilsmoke-session-log',
                 (P/'package/lilsmoke-scripts/src/lilsmoke-session-log').read_bytes(),0o755)
    overlay.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
    overlay.file('etc/lilsmoke/input-maps/default.map',
                 (P/'board/qemu-mipsel/input-maps/default.map').read_bytes())
    runtime_libraries(B,overlay,[ui,helper])
    initrd=Q/'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(B,overlay,'/usr/bin/bench-init',overlay_mib=32)))
    command=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
        '-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','120',
        '-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),
        '-append','console=ttyS0 video=cirrusfb:856x480M-16@60 panic=-1',
        '-vga','cirrus','-display','none','-serial','stdio','-monitor','none','-no-reboot',
        '-device','usb-kbd','-qmp',f'unix:{Q}/qmp.sock,server=on,wait=off',
        *disk_devices(B,[('bench',Q,False),('apps',args.apps,False)])]
    def read(name):
        path=Q/name
        return path.read_text(errors='replace') if path.exists() else ''
    qmp=None
    stage=0
    with (Q/'serial.log').open('wb') as log:
        vm=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+900
            while vm.poll() is None and 'reboot: System halted' not in read('serial.log'):
                if time.monotonic()>deadline:
                    raise RuntimeError('Public-cache gameplay VM timed out; inspect logs')
                if qmp is None and (Q/'qmp.sock').exists(): qmp=Qmp(Q/'qmp.sock')
                ui_log=read('ui.log')
                engine=read('sm64-public.log')
                if stage==0 and 'UI: entering main loop' in ui_log:
                    time.sleep(1)
                    qmp.call('screendump',dict(filename=str(Q/'home.ppm')))
                    qmp.key('ret')
                    stage=1
                elif stage==1 and 'Applications: catalog ready (1)' in ui_log:
                    time.sleep(.5)
                    qmp.call('screendump',dict(filename=str(Q/'applications.ppm')))
                    qmp.key('ret')
                    stage=2
                elif stage==2 and 'OriginalFrame=3000 ' in engine:
                    qmp.call('send-key',dict(keys=[dict(type='qcode',data='up'),dict(type='qcode',data='ctrl')],**{'hold-time':2500}))
                    stage=3
                elif stage==3 and 'OriginalFrame=3060 ' in engine:
                    qmp.call('send-key',dict(keys=[dict(type='qcode',data='ret'),dict(type='qcode',data='spc')],**{'hold-time':300}))
                    stage=4
                elif stage==4 and 'SM64_MENU opened' in engine:
                    time.sleep(.5)
                    qmp.call('screendump',dict(filename=str(Q/'menu.ppm')))
                    qmp.key('alt')
                    stage=5
                elif stage==5 and 'OriginalEngineTest=FINISHED' in engine:
                    time.sleep(2)
                    qmp.call('screendump',dict(filename=str(Q/'returned-applications.ppm')))
                    qmp.key('alt');time.sleep(.5)
                    qmp.key('left');time.sleep(.5)
                    qmp.key('ret')
                    stage=6
                time.sleep(.25)
        finally:
            if qmp: qmp.close()
            if vm.poll() is None: vm.terminate()
            try: vm.wait(timeout=10)
            except subprocess.TimeoutExpired: vm.kill();vm.wait()
    engine=read('sm64-public.log')
    assert stage==6 and 'QA_UI_EXIT=0' in read('serial.log'),read('serial.log')[-1500:]+read('ui.log')[-1500:]+engine[-1500:]
    assert 'NativeLoadingIcon=WebP Size=128x128' in engine
    assert 'Frames=3360 Rendered=3360' in engine
    assert re.search(r'OriginalFrame=\d+ Level=16 Area=1',engine)
    assert re.search(r'AudioNonzero=[1-9]\d*',engine)
    assert 'OriginalNativeInput' in engine and 'SM64_MENU resumed' in engine
    images=[]
    for path in Q.glob('*.ppm'):
        with Image.open(path) as image:
            image.save(path.with_suffix('.png'))
        images.append(path.with_suffix('.png').name)
    memory=read('process.log')
    def peak(field): return max([0]+[int(x) for x in re.findall(r'^'+field+r':\s+(\d+) kB',memory,re.M)])
    result=dict(passed=True,production_ui=True,production_apps_router=True,prepared_public_cache=True,
        frames=3360,render_stride=1,original_engine=True,native_webp=True,native_input=True,
        options_menu_and_return=True,audio_synthesis_nonzero=True,images=images,
        cpus=1,guest_ram_mib=120,process_address_limit_mib=64,
        sampled_game_rss_kib=peak('VmRSS'),sampled_game_vmpeak_kib=peak('VmPeak'),
        memory_sample_seconds=5,physical_hc16xx_tested=False,physical_fps_measured=False,
        fresh_first_run_cooked_in_this_vm=False,audible_hardware_audio_tested=False)
    (Q/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
