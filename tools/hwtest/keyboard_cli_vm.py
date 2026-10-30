#!/usr/bin/env python3
"""Native text-request component test inside BusyBox/MIPS Linux, not a game test."""
import argparse
import gzip
import json
from pathlib import Path
import socket
import subprocess
import time
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs, disk_devices

ROOT=Path(__file__).resolve().parents[2]
def main():
    p=argparse.ArgumentParser()
    p.add_argument('--build',type=Path,default=Path('/root/lilsmoke-os/build/buildroot/output'))
    p.add_argument('--qemu',required=True); p.add_argument('--out',type=Path,required=True)
    a=p.parse_args(); out=a.out.resolve(); out.mkdir(parents=True,exist_ok=False)
    persist=out/'persist'; storage=out/'storage'; persist.mkdir(); storage.mkdir()
    overlay=Newc()
    overlay.file('etc/inittab','''::sysinit:/etc/init.d/rcS
tty1::once:/usr/bin/keyboard-qa
::ctrlaltdel:/sbin/reboot
::shutdown:/etc/init.d/rcK
''')
    overlay.file('usr/bin/keyboard-qa','''#!/bin/sh
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=alsa
export LILSMOKE_DFB_EVDEV_KEYCODES=1
export DFBARGS=no-hardware,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
unset DISPLAY WAYLAND_DISPLAY
exec /usr/bin/lilsmoke-keyboard --title Name --max-length 16 >/persist/keyboard-result 2>/persist/keyboard-cli.log
''',0o755)
    overlay.file('usr/bin/lilsmoke-keyboard',(a.build/'target/usr/bin/lilsmoke-keyboard').read_bytes(),0o755)
    overlay.file('usr/lib/libSDL2-2.0.so.0.2800.5',(a.build/'target/usr/lib/libSDL2-2.0.so.0.2800.5').read_bytes(),0o755)
    overlay.file('etc/directfbrc',(ROOT/'board/qemu-mipsel/directfbrc').read_bytes())
    overlay.file('etc/init.d/S02mdev',(ROOT/'board/qemu-mipsel/init.d/S02mdev').read_bytes(),0o755)
    image=out/'keyboard.cpio.gz'
    image.write_bytes(gzip.compress(overlay_initramfs(a.build,overlay)))
    sock=out/'qmp.sock'; events=[]
    def qmp(name,args=None):
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
            s.settimeout(20); s.connect(str(sock))
            with s.makefile('rwb',buffering=0) as f:
                json.loads(f.readline())
                for request in ({'execute':'qmp_capabilities'},{'execute':name,'arguments':args or {}}):
                    f.write(json.dumps(request).encode()+b'\n')
                    while True:
                        reply=json.loads(f.readline())
                        if 'error' in reply: raise RuntimeError(reply)
                        if 'return' in reply: break
                return reply['return']
    def capture(name):
        qmp('screendump',{'filename':str(out/(name+'.ppm'))})
        subprocess.run(['convert',str(out/(name+'.ppm')),str(out/(name+'.png'))],check=True)
    def key(name):
        qmp('human-monitor-command',{'command-line':'sendkey '+name+' 120'})
        events.append({'key':name,'hold_ms':120,'wall_seconds':time.time()}); time.sleep(.6)
    command=[a.qemu,'-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','128',
        '-accel','tcg,thread=single','-kernel',str(a.build/'images/vmlinux'),'-initrd',str(image),
        '-append','console=ttyS0 video=cirrusfb:856x480M-24@60 panic=-1',
        '-vga','cirrus','-display','none','-serial','stdio','-no-reboot','-device','usb-kbd',
        '-qmp',f'unix:{sock},server=on,wait=off']
    command+=disk_devices(a.build,[('persist',persist,False),('storage',storage,False)])
    with (out/'serial.log').open('wb') as log:
        vm=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+240
            ready=persist/'keyboard-cli.log'
            while not ready.exists() or 'Font: loaded' not in ready.read_text(errors='replace'):
                if vm.poll() is not None or time.monotonic()>deadline: raise RuntimeError('keyboard did not open')
                time.sleep(1)
            time.sleep(2); capture('01-keyboard')
            for button in ('ctrl','down','ctrl'): key(button)
            time.sleep(1); capture('02-text'); key('ret')
            result=persist/'keyboard-result'; deadline=time.monotonic()+60
            while not result.exists() or result.read_text()!='1q\n':
                if time.monotonic()>deadline: raise RuntimeError('keyboard commit differs: '+(result.read_text() if result.exists() else 'missing'))
                time.sleep(1)
        finally:
            if vm.poll() is None: vm.terminate()
            try: vm.wait(timeout=10)
            except subprocess.TimeoutExpired: vm.kill(); vm.wait()
    (out/'result.json').write_text(json.dumps({'result':'1q','events':events,'command':command,
        'notice':'Native keyboard component test, not a UI game recording or physical FPS benchmark.'},indent=2))
    print('KEYBOARD_CLI_VM_OK',flush=True)
if __name__=='__main__': main()
