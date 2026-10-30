"""Actual Explorer -> MIPS media player -> Explorer, CPU-only Linux/DirectFB."""
import gzip
import hashlib
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from PIL import Image

P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('out',type=Path)
parser.add_argument('selected',choices=('Clip.mp4','One.wav','One.mp3','Photo.webp'))
parser.add_argument('--binary',type=Path,required=True)
parser.add_argument('--ui',type=Path,required=True)
parser.add_argument('--fixtures',type=Path,default=Path('/root/lilsmoke-media-fixtures'))
parser.add_argument('--rootfs',type=Path)
args=parser.parse_args()
out=args.out.resolve()
selected=args.selected
out.mkdir(exist_ok=False)
sys.path.insert(0,str(P/'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
sys.path.insert(0,str(P/'tools/portsmoke/sm64'))
from vm_scene_smoke import Qmp
import shutil
shutil.copytree(args.fixtures,out/'fixtures')
init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /persist/config /persist/logs /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
ln -s /bench/media-player.log /persist/logs/media-player.log
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_AUDIO_OVERRIDE=dummy LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_EXPLORER_RETURN=/bench/fixtures LILSMOKE_EXPLORER_FOCUS=/bench/fixtures/Clip.mp4
cat /proc/meminfo >/bench/memory.txt
(
    while :; do
        sleep 2
        for file in /proc/[0-9]*/comm; do
            read -r name < "$file" || continue
            [ "$name" = lilsmoke-media ] || continue
            cat "${file%/comm}/status"
        done
    done
) >/bench/process.log 2>&1 &
/usr/bin/lilsmoke-ui >/bench/ui.log 2>&1
echo MEDIA_UI_EXIT=$?
sync
poweroff -f
'''
fixture=Newc();fixture.file('usr/bin/bench-init',init,0o755)
fixture.entries[-1].data=fixture.entries[-1].data.replace(b'FOCUS=/bench/fixtures/Clip.mp4',('FOCUS=/bench/fixtures/'+selected).encode())
ui=args.ui;media=args.binary
fixture.file('usr/bin/lilsmoke-ui',ui.read_bytes(),0o755)
fixture.file('usr/bin/lilsmoke-media',media.read_bytes(),0o755)
fixture.file('usr/lib/lilsmoke/lilsmoke-media-open',(P/'package/lilsmoke-media/lilsmoke-media-open').read_bytes(),0o755)
fixture.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
fixture.file('persist/config/lilsmoke.conf','DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\nSCAN_ROMS_ON_BOOT=off\nEMULATOR_BACKEND=retroarch\nDEFAULT_THEME=default\nMASTER_VOLUME=50\nPOINTER_ENABLED=off\n')
root=args.rootfs.resolve() if args.rootfs else out/'rootfs.squashfs'
if not args.rootfs:
    subprocess.run([str(B/'host/bin/mksquashfs'),str(B/'target'),str(root),'-noappend','-comp','gzip','-processors','1','-no-xattrs'],check=True,stdout=subprocess.DEVNULL)
# Libraries are read from a disk-backed OS, just as on the console, not copied
# twice into the guest's limited RAM by an oversized diagnostic initramfs.
(out/'boot.cpio.gz').write_bytes(gzip.compress(overlay_initramfs(B,fixture,'/usr/bin/bench-init',overlay_mib=16)))
devices=disk_devices(B,[('bench',out,False)])
devices=[part.replace(str(B/'images/rootfs.squashfs'),str(root)) for part in devices]
command=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','120',
    '-kernel',str(B/'images/vmlinux'),'-initrd',str(out/'boot.cpio.gz'),'-append','console=ttyS0 video=cirrusfb:640x480M-24@60 panic=-1',
    '-vga','cirrus','-display','none','-serial','stdio','-monitor','none','-no-reboot','-device','usb-kbd',
    '-qmp',f'unix:{out}/qmp.sock,server=on,wait=off',*devices]
def read(name):
    file=out/name
    try:return file.read_text(errors='replace') if file.exists() else ''
    except OSError:return ''
def wait_for(predicate,label,seconds=180):
    deadline=time.monotonic()+seconds
    while not predicate():
        if vm.poll() is not None or time.monotonic()>deadline:
            raise RuntimeError(label+'\n'+read('serial.log')[-2000:]+'\n'+read('ui.log')[-2000:]+'\n'+read('media-player.log')[-2000:])
        time.sleep(.3)
events=[]
with (out/'serial.log').open('wb') as log:
    vm=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    qmp=None
    try:
        wait_for(lambda:'UI: entering main loop' in read('ui.log'),'Explorer did not start')
        qmp=Qmp(out/'qmp.sock')
        def key(code,delay=1):
            qmp.call('send-key',dict(keys=[dict(type='qcode',data=code)],**{'hold-time':150}));events.append(code);time.sleep(delay)
        def capture(name):
            deadline=time.monotonic()+20
            while True:
                qmp.call('screendump',{'filename':str(out/(name+'.ppm'))})
                im=Image.open(out/(name+'.ppm'))
                if len(im.getcolors(im.width*im.height) or [])>6:break
                if time.monotonic()>deadline:raise RuntimeError('Blank guest capture: '+name)
                time.sleep(.4)
            im.save(out/(name+'.png'))
        time.sleep(2);capture('01-explorer')
        key('ctrl',2)
        wait_for(lambda:'MEDIA_OPEN' in read('media-player.log'),'Explorer did not launch media')
        time.sleep(10);capture('02-playing')
        play_log=read('media-player.log')
        assert 'MEDIA_OPEN' in play_log
        if selected=='Photo.webp':
            key('pgdn');key('right');key('pgup');capture('03-photo-zoom')
        else:
            key('ret');wait_for(lambda:'MEDIA_ACTION id=0' in read('media-player.log'),'Native Start did not pause')
            capture('03-paused');key('ret');key('right');key('left')
        key('spc');capture('04-options');key('down');key('ctrl');key('alt');key('alt',2)
        wait_for(lambda:read('ui.log').count('UI: resumed Explorer')>=2,'Media exit did not restore Explorer')
        capture('05-return-explorer')
        if selected!='Photo.webp':assert 'MEDIA_SEEK' in read('media-player.log')
        assert 'MEDIA_DONE' in read('media-player.log')
        if selected not in ('One.wav','One.mp3'):assert 'decoded=0' not in read('media-player.log').split('MEDIA_DONE')[-1]
        if selected in ('One.wav','One.mp3','Clip.mp4'):
            import re
            done=read('media-player.log').split('MEDIA_DONE')[-1]
            assert int(re.search(r'audio_frames=(\d+)',done)[1])>22050,done
        if selected=='Clip.mp4':assert int(re.search(r'decoded=(\d+)',done)[1])>3,done
    finally:
        if qmp:qmp.close()
        if vm.poll() is None:vm.terminate()
        try:vm.wait(timeout=10)
        except subprocess.TimeoutExpired:vm.kill();vm.wait()
report={'passed':True,'production_ui_and_player':True,'opened_from_explorer':True,
    'binary_sha256':hashlib.sha256(media.read_bytes()).hexdigest(),
    'native_guest_keyboard_to_controller_path':True,'pause_seek_options_and_return':True,
    'cpus':1,'ram_mib':120,'gpu_3d':False,'directfb_software':True,'fixtures_directory':str(args.fixtures),
    'audio_device':'SDL dummy (no actual M8 auddec in Malta)','physical_console_fps_measured':False,'events':events,'selected_file':selected}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS real MIPS Explorer/media/native-input/return workflow:',out)
