#!/usr/bin/env python3
"""Native package launcher + QMP controls in private constrained MIPS Linux.

No Windows standalone game, physical disk access or HC16xx FPS claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import zipfile
from PIL import Image
from vm_scene_smoke import Qmp, initramfs, run

BOOT=r'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
/bin/busybox --install -s /bin
export PATH=/bin:/usr/bin
mkdir -p /proc /sys /dev /tmp /run /storage
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
echo "NATIVE_VM_MEM_KB=$(awk '/MemTotal:/ {print $2}' /proc/meminfo)"
disk=/dev/sda
[ -b "$disk" ] || disk=/dev/hda
mount -t vfat "$disk" /storage || { echo 'FAIL mount'; poweroff -f; }
APP='/storage/independent-apps/SM64 Native Prototype'
export LILSMOKE_APP_DIR="$APP" LD_LIBRARY_PATH="$APP/runtime/lib:/usr/lib:/lib"
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software
export SDL_AUDIODRIVER=dummy
export DFBARGS='system=fbdev,fbdev=/dev/fb0,no-hardware,no-cursor,no-sighandler,quiet,no-vt,mode=640x480,depth=24,pixelformat=RGB24,disable-module=keyboard,linux-input-force'
cd "$APP"
mkdir -p .sm64
FAIL=0
echo NATIVE_PACKAGE_LAUNCH_BEGIN
(ulimit -v 49152; /bin/sh ./SM64.sh)
[ "$?" -eq 0 ] || FAIL=1
echo NATIVE_PACKAGE_LAUNCH_END
[ ! -f original/'any name.private' ] || FAIL=1
(ulimit -v 32768; /usr/bin/scene-qa --original original --pack .sm64/bob-us-v1.pdat --icon icon.webp --headless --frames 150 --gameplay-check --snapshot /storage/headless.bmp)
[ "$?" -eq 0 ] || FAIL=1
sync
cd /
umount /storage || FAIL=1
echo "NATIVE_VM_RESULT=$FAIL"
poweroff -f
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','qemu','qa','package','rom','out'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--version',default='0.4.0')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    before=hashlib.sha256(args.rom.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix='sm64-native-game-vm-',dir='/root') as temporary:
        work=Path(temporary);mount=work/'mount';mount.mkdir();disk=work/'private.fat'
        with disk.open('wb') as f:f.truncate(64*1024*1024)
        run(['mkfs.fat','-F','32',disk]);run(['mount','-o','loop',disk,mount])
        try:
            app=mount/'independent-apps/SM64 Native Prototype';app.mkdir(parents=True)
            with zipfile.ZipFile(args.package) as archive:
                meta=json.loads(archive.read('meta.json'));assert meta['version']==args.version and meta['name']=='SM64 Native Prototype'
                assert all(not x.startswith('/') and '..' not in Path(x).parts for x in archive.namelist())
                archive.extractall(app)
            shutil.copyfile(args.rom,app/'original/any name.private')
        finally:run(['umount',mount])
        ramdisk=work/'init.cpio.gz';initramfs(args.sdk,args.qa,ramdisk,BOOT)
        sock=work/'qmp.sock';log=work/'serial.log';pictures=work/'frames';pictures.mkdir()
        command=[str(args.qemu),'-M','malta','-cpu','74Kf-lilsmoke','-m','120','-smp','1',
                 '-kernel',str(args.sdk/'images/vmlinux'),'-initrd',str(ramdisk),'-append','console=ttyS0 rdinit=/init',
                 '-drive',f'file={disk},format=raw,if=ide','-vga','cirrus','-device','usb-kbd','-display','none',
                 '-serial','stdio','-monitor','none','-qmp',f'unix:{sock},server=on,wait=off','-no-reboot']
        # Real QMP input, not SDL_PushEvent: movement, jump, pause/resume, restart and exit.
        actions=[(0.8,'up',1400),(2.5,'ctrl',900),(4.8,'ret',120),(6.1,'ret',120),
                 (7.4,'ret',120),(7.8,'down',120),(8.2,'ctrl',120),(11.5,'esc',120)]
        qmp=None;ready=None;done=0;frame=0;capture=0;selected={};loading=False;loading_middle=False
        with log.open('w') as output:
            process=subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL)
            try:
                deadline=time.monotonic()+180
                while process.poll() is None and time.monotonic()<deadline:
                    text=log.read_text(errors='replace')
                    if 'NativeExtractionUIReady=1' in text and qmp is None:qmp=Qmp(sock)
                    if qmp and ready is None and time.monotonic()>=capture:
                        picture=pictures/f'{frame:05d}.ppm';qmp.call('screendump',{'filename':str(picture)})
                        frame+=1;capture=time.monotonic()+0.1
                        image=Image.open(picture).convert('RGB')
                        blue=sum(1 for r,g,b in image.getdata() if abs(r-24)<6 and abs(g-142)<6 and abs(b-220)<6)
                        if not loading_middle:
                            image.save(args.out/'extraction.png')
                            loading_middle=80<blue<3000
                        loading=True
                    if 'NativeGameInputReady=1' in text and ready is None:ready=time.monotonic();capture=0
                    if ready is not None:
                        elapsed=time.monotonic()-ready
                        while done<len(actions) and elapsed>=actions[done][0]:
                            _,key,hold=actions[done]
                            qmp.call('send-key',dict(keys=[dict(type='qcode',data=key)],**{'hold-time':hold}));done+=1
                        if elapsed>=capture and 'NATIVE_PACKAGE_LAUNCH_END' not in text:
                            picture=pictures/f'{frame:05d}.ppm';qmp.call('screendump',{'filename':str(picture)})
                            frame+=1;capture=elapsed+0.1
                            for name,at in [('spawn',0),('walking',2),('jump',3),('pause',5.4),('restart',9.2)]:
                                if elapsed>=at and name not in selected:
                                    Image.open(picture).save(args.out/(name+'.png'));selected[name]=elapsed
                    if 'NATIVE_VM_RESULT=' in text and ('System halted' in text or 'Power down' in text):break
                    time.sleep(0.02)
                text=log.read_text(errors='replace')
                assert 'NATIVE_VM_RESULT=0' in text and 'NativeGameplayCheck=PASS' in text,text[-7000:]
                assert 'NativeGamePresentation=SDL-window Driver=directfb' in text,text[-7000:]
                assert done==len(actions) and len(selected)==5,(done,selected)
                stats=re.findall(r'NativeGameFrames=(\d+).*PlayerJumps=(\d+).*PlayerLandings=(\d+).*Pauses=(\d+).*PausedFrames=(\d+)',text)
                assert len(stats)==2 and int(stats[0][1])>=1 and int(stats[0][3])>=2 and int(stats[0][4])>0,stats
                assert loading and 'OriginalRemoved=1' in text and 'UsedCache=1' in text,text[-7000:]
            finally:
                if qmp:qmp.close()
                if process.poll() is None:process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
                shutil.copyfile(log,args.out/'serial.log')
        fsck=run(['fsck.fat','-n',disk]);(args.out/'fsck.txt').write_text(fsck)
        run(['mount','-o','loop,ro',disk,mount])
        try:
            assert not (mount/'independent-apps/SM64 Native Prototype/original/any name.private').exists()
            Image.open(mount/'headless.bmp').save(args.out/'headless.png')
        finally:run(['umount',mount])
        if shutil.which('ffmpeg'):
            run(['ffmpeg','-y','-loglevel','error','-framerate','10','-i',pictures/'%05d.ppm',
                 '-c:v','libvpx-vp9','-b:v','600k','-pix_fmt','yuv420p',args.out/'sm64-native-mips.webm'])
        result=dict(passed=True,cpus=1,ram_mib=120,guest_usable_kib=int(re.search(r'NATIVE_VM_MEM_KB=(\d+)',text)[1]),
                    peak_rss_kib=list(map(int,re.findall(r'NativeGamePeakRSSKB=(\d+)',text))),
                    address_space_caps_mib=[32,48],pc_original_rom_unchanged=True,private_app_original_deleted_after_verification=True,
                    extraction_ui_and_native_webp=True,cpu_rendering=True,
                    production_package_launcher=True,qmp_input_actions=actions,frames_captured=frame,
                    full_game=False,physical_m8_tested=False,hardware_fps_claimed=False,
                    video_is_guest_screen_capture=True,video_not_hardware_benchmark=True)
        (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest()==before

if __name__=='__main__':main()
