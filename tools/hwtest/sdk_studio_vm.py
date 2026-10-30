#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Real generated SDK app through card UI/installer on disposable MIPS/FAT32.
Display: DirectFB/Cirrus, not HC scanout. Audio: SDL dummy, not physical DAC.
QMP input is isolated to QEMU; no desktop keys or host driver changes.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from PIL import Image

P=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(P/'tools/hwtest'))
sys.path.insert(0,str(P/'tools/portsmoke/sm64'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
from vm_scene_smoke import Qmp

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('package','snapshot','out'): ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--sdk',type=Path,default=Path('/root/lilsmoke-os/build/buildroot/output'))
    a=ap.parse_args(); a.out=a.out.resolve(); a.out.mkdir(parents=True,exist_ok=False)
    with zipfile.ZipFile(a.package) as z:
        meta=json.loads(z.read('meta.json'))
        engine=hashlib.sha256(z.read('runtime/bin/application')).hexdigest()
        launchers=[name for name in z.namelist() if '/' not in name
                   and Path(name).suffix.lower()=='.sh']
        assert len(launchers)==1, 'Package must have exactly one root shell launcher'
    snap=json.loads((a.snapshot/'snapshot.json').read_text()); assert snap['card_writes']==0
    report=dict(passed=False,package_sha256=sha(a.package),engine_sha256=engine,
        root_launcher=launchers[0],
        cpu='74Kf',cpus=1,ram_mib=128,gpu_3d=False,physical_hardware=False,
        storage='temporary FAT32 virtio disk',audio_sink='SDL dummy, not physical DAC',
        input='QMP USB keyboard/tablet, not host input injection',events=[],screenshots={},runs=[],
        pointer_coordinates='raw ABS pixels for DirectFB linux-input tablet fixture',
        source_manifest_sha256=snap['source_manifest_sha256'],
        actual_ui_sha256=next(x['sha256'] for x in snap['files'] if x['target']=='usr/bin/lilsmoke-ui'))
    vm=qmp=None
    with tempfile.TemporaryDirectory(prefix='sdk-studio-vm-') as t:
        w=Path(t); bench=w/'bench'; bench.mkdir()
        shutil.copyfile(a.package,bench/'studio.lilpck')
        disk=w/'storage.fat'
        with disk.open('xb') as f: f.truncate(128*1024*1024)
        subprocess.run(['mkfs.fat','-F','32','-n','SDK_QA',str(disk)],check=True,capture_output=True)
        ov=Newc(); binaries=[]
        for item in snap['files']:
            p=a.snapshot/item['target']; assert sha(p)==item['sha256']; data=p.read_bytes()
            ov.file(item['target'],data,0o755)
            if data[:4]==b'\x7fELF': binaries.append(p)
        runtime_libraries(a.sdk,ov,binaries)
        ov.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
        ov.file('persist/config/lilsmoke.conf','DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\n'
            'SCAN_ROMS_ON_BOOT=on\nDEFAULT_THEME=default\nPOINTER_ENABLED=off\n'
            'UI_RENDER_MODE=native\nSHOW_CLOCK=off\nSHOW_BATTERY=off\n')
        init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /storage /persist/logs /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=4m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t vfat -o rw,umask=0077 /dev/vdb /storage || poweroff -f
mkdir -p /storage/roms /storage/independent-apps
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software
export SDL_AUDIODRIVER=dummy LILSMOKE_AUDIO_OVERRIDE=dummy
export LILSMOKE_SDK_TRACE=1 LILSMOKE_DFB_EVDEV_KEYCODES=1 LILSMOKE_HCFB=0
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
cat /proc/meminfo > /bench/memory.txt
/usr/lib/lilsmoke/lilsmoke-apps install /bench/studio.lilpck PACKAGE_HASH > /bench/install.log 2>&1
echo SDK_INSTALL_EXIT=$?
(while [ ! -f /bench/finish ]; do
    for path in /proc/[0-9]*/comm; do
        name=$(cat "$path" 2>/dev/null) || continue
        case "$name" in application)
            proc=$(dirname "$path")
            cat "$proc/status" >> /bench/app-memory.log
            if cat "$proc/maps" > /tmp/sdk-app-maps && [ -s /tmp/sdk-app-maps ]; then
                cat /tmp/sdk-app-maps > /bench/app-maps.txt
            fi
            ;;
        esac
    done
    sleep 1
done
kill -TERM "$(cat /bench/ui.pid)" 2>/dev/null) &
watcher=$!
while :; do
    /usr/bin/lilsmoke-ui >> /bench/ui.log 2>&1 &
    ui=$!; echo "$ui" > /bench/ui.pid
    wait "$ui"; echo SDK_UI_EXIT=$?
    [ ! -f /bench/finish ] || break
    sleep 0.2
done
wait "$watcher"
sync
if umount /storage; then echo SDK_STORAGE_UNMOUNTED; fi
echo SDK_STUDIO_VM_DONE
poweroff -f
'''.replace('PACKAGE_HASH',report['package_sha256'])
        ov.file('usr/bin/bench-init',init,0o755)
        initrd=w/'boot.cpio.gz'
        initrd.write_bytes(gzip.compress(overlay_initramfs(a.sdk,ov,'/usr/bin/bench-init',overlay_mib=16)))
        devices=disk_devices(a.sdk,[('bench',bench,False)])
        devices=[s.replace(str(a.sdk/'images/rootfs.squashfs'),
            '/root/lilsmoke-media-c45-final-video/rootfs.squashfs') for s in devices]
        devices+=['-drive',f'file={disk},if=none,id=sdkdisk,format=raw',
                  '-device','virtio-blk-pci,drive=sdkdisk,addr=0x15']
        cmd=['taskset','-c',str(min(os.sched_getaffinity(0))),'nice','-n','15',
            '/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
            '-M','malta','-cpu','74Kf','-smp','1','-m','128','-kernel',str(a.sdk/'images/vmlinux'),
            '-initrd',str(initrd),'-append','console=ttyS0 panic=-1','-vga','cirrus','-display','none',
            '-serial','stdio','-monitor','none','-no-reboot','-device','usb-kbd','-device','usb-tablet',
            '-qmp',f'unix:{w}/qmp.sock,server=on,wait=off',*devices]
        report['command']=cmd
        def read(n):
            p=bench/n; return p.read_text(errors='replace') if p.is_file() else ''
        def wait(check,label,seconds=120):
            end=time.monotonic()+seconds
            while not check():
                if vm.poll() is not None or time.monotonic()>=end:
                    raise RuntimeError(label+'\n'+read('ui.log')[-6000:]+'\n'+read('serial.log')[-2000:])
                time.sleep(.15)
        def key(n,delay=.4):
            # Release on the guest timer, not a variable host scheduling delay.
            qmp.call('send-key',{'keys':[dict(type='qcode',data=n)],'hold-time':40})
            report['events'].append(n); time.sleep(delay)
        def states(prefix):
            return [json.loads(s[len(prefix):]) for s in read('ui.log').splitlines() if s.startswith(prefix)]
        def capture(n):
            end=time.monotonic()+20
            while True:
                ppm=w/(n+'.ppm'); qmp.call('screendump',dict(filename=str(ppm)))
                with Image.open(ppm) as im:
                    colors=im.getcolors(im.width*im.height) or []
                    if len(colors)>=6 and sum(c for c,rgb in colors if rgb!=(0,0,0))>im.width*im.height//10:
                        im.save(a.out/(n+'.png'))
                        report['screenshots'][n]=dict(colors=len(colors),sha256=sha(a.out/(n+'.png')))
                        return
                if time.monotonic()>end: raise RuntimeError('Blank/flat capture: '+n)
                time.sleep(.2)
        def launch(n):
            wait(lambda:read('ui.log').count('SystemView: first frame complete')>=n,'Root frame missing')
            key('ctrl',.5)
            wait(lambda:read('ui.log').count('Applications: catalog ready (1)')>=n,'Application catalogue not ready')
            capture(str(n)+'-applications')
            key('ctrl',1.0)
            wait(lambda:len(states('SDK_STUDIO_READY '))>=n,'Generated app not launched from Applications')
        def quit_menu():
            for n in ('ret','down','down','ctrl','right','ctrl'): key(n)
        try:
            with (bench/'serial.log').open('wb') as log:
                vm=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
                wait(lambda:'SDK_INSTALL_EXIT=0' in read('serial.log') and 'UI: entering main loop' in read('ui.log'),'Installer/UI startup failed')
                qmp=Qmp(w/'qmp.sock')
                wait(lambda:'SystemView: first frame complete' in read('ui.log'),'Initial root frame missing')
                capture('01-home'); launch(1); capture('02-studio')
                for n in ('right','down','ctrl'): key(n)
                wait(lambda:any(s['tone_queued'] and s['nonzero_samples'] for s in states('SDK_STUDIO_STATE ')),'No accepted nonzero SDL audio')
                for n in ('down','right','down','ctrl'): key(n)
                capture('03-settings-audio')
                # This DirectFB linux-input backend passes USB tablet ABS units
                # through as pixels; it does not normalize QMP's 0..32767 range.
                events=[dict(type='abs',data=dict(axis='x',value=420)),
                        dict(type='abs',data=dict(axis='y',value=120))]
                qmp.call('input-send-event',dict(events=events)); time.sleep(.2)
                for down in (True,False):
                    qmp.call('input-send-event',dict(events=[dict(type='btn',data=dict(button='left',down=down))]))
                    time.sleep(.15)
                report['events'].append('tablet-click')
                wait(lambda:any(s['mouse_events'] and s['focus']==0 and s['color']==2
                                for s in states('SDK_STUDIO_STATE ')),
                     'Pointer click did not activate the color row')
                key('ret'); capture('04-paused'); key('ctrl'); quit_menu()
                wait(lambda:len(states('SDK_STUDIO_EXIT '))>=1,'Clean app exit missing')
                first=states('SDK_STUDIO_EXIT ')[0]
                assert first['clean_exit'] and first['saves'] and first['tone_queued'] and first['nonzero_samples'],first
                assert first['keyboard_events']>0 and first['mouse_events']>0,first
                assert first['color']==2 and first['tone']==1 and first['volume']==40,first
                launch(2); second=states('SDK_STUDIO_READY ')[1]
                assert second['loaded']==1 and all(second[k]==first[k] for k in ('color','tone','volume')),second
                capture('05-reloaded-settings'); quit_menu()
                wait(lambda:len(states('SDK_STUDIO_EXIT '))>=2 and read('ui.log').count('SystemView: first frame complete')>=3,'Second UI return missing')
                report['runs']=states('SDK_STUDIO_EXIT '); capture('06-return')
                maps=read('app-maps.txt')
                assert '/runtime/bin/application' in maps,'Application process was not observed'
                assert '/usr/bin/lilsmoke-ui' not in maps,'UI image remained in app process'
                resident=[int(n) for n in re.findall(r'^VmRSS:\s+(\d+) kB$',read('app-memory.log'),re.M)]
                assert resident,'Application memory samples missing'
                report['app_peak_sampled_rss_kib']=max(resident)
                assert max(resident)<32*1024,'SDK example exceeded its 32 MiB resident budget'
                (bench/'finish').touch()
                wait(lambda:'SDK_STUDIO_VM_DONE' in read('serial.log'),'Storage shutdown missing')
                report.update(install_launch_input_audio_save_reload_return=True,no_resident_ui=True,
                    storage_unmounted='SDK_STORAGE_UNMOUNTED' in read('serial.log'))
        except Exception as e:
            report['error']=str(e); raise
        finally:
            if qmp: qmp.close()
            if vm and vm.poll() is None: vm.terminate()
            if vm:
                try: vm.wait(timeout=10)
                except subprocess.TimeoutExpired: vm.kill(); vm.wait(timeout=5)
            for p in bench.iterdir():
                if p.is_file() and p.name!='studio.lilpck': shutil.copyfile(p,a.out/p.name)
            result=subprocess.run(['fsck.fat','-n',str(disk)],capture_output=True,text=True)
            (a.out/'fat32-check.txt').write_text(result.stdout+result.stderr)
            report['fat32_check_exit']=result.returncode
            if report.get('storage_unmounted'):
                data=subprocess.check_output(['mtype','-i',str(disk),'::/independent-apps/'+meta['name']+'/userdata/studio.ini'])
                (a.out/'saved-studio.ini').write_bytes(data)
                report['saved_settings_sha256']=hashlib.sha256(data).hexdigest()
            report['passed']=bool(report.get('install_launch_input_audio_save_reload_return') and report.get('storage_unmounted') and result.returncode==0)
            (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__': main()

