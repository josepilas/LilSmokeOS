#!/usr/bin/env python3
"""Normal BusyBox/UI boot, isolated storage, QMP-only keys and screenshots.

This is a functional test, not a hardware speed benchmark. No host desktop
input, user save folders, accelerated graphics or substitute frontend.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import signal
import shutil
import socket
import subprocess
import time
from mips_boot_smoke import Newc

ROOT=Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--build',type=Path,default=Path('/root/lilsmoke-os/build/buildroot/output'))
    p.add_argument('--qemu',default='qemu-system-mipsel')
    p.add_argument('--cpu',choices=['74Kf','74Kf-lilsmoke'],default='74Kf')
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--core',type=Path,required=True)
    p.add_argument('--retroarch',type=Path)
    p.add_argument('--ui',type=Path)
    p.add_argument('--sdl',type=Path)
    p.add_argument('--directfb-input',type=Path)
    p.add_argument('--record',action='store_true',help='15 FPS silent WebM of the actual VM display; not a timing benchmark')
    p.add_argument('--trace-input',action='store_true',help='diagnostic SDL key-state trace; invalidates performance comparisons')
    p.add_argument('--trace-vt',action='store_true',help='log active Linux terminal during functional keyboard tests; not a timing run')
    p.add_argument('--hold',action='store_true',help='after game boot, leave the VM running for manual QMP-only input until QMP quit')
    p.add_argument('--ram',type=int,choices=[128,256],default=256)
    a=p.parse_args()
    out=a.out.resolve()
    out.mkdir(parents=True,exist_ok=False)
    storage=out/'storage'; persist=out/'persist'
    (storage/'roms/snes').mkdir(parents=True)
    (persist/'config').mkdir(parents=True)
    shutil.copyfile(a.archive,storage/'roms/snes'/a.archive.name)
    (persist/'config/lilsmoke.conf').write_text('''DISPLAY_WIDTH=856
DISPLAY_HEIGHT=480
DISPLAY_ROTATION=0
DEFAULT_THEME=default
SHOW_FPS=1
SCALING_MODE=integer
SCAN_ROMS_ON_BOOT=1
CPU_OVERCLOCK=stock
ZRAM_ENABLED=off
WIFI_ENABLED=off
SSH_ENABLED=off
''')
    overlay=Newc()
    overlay.dir('usr/share/libretro')
    overlay.dir('usr/share/libretro/info')
    overlay.dir('usr/share/lilsmoke')
    overlay.dir('usr/share/lilsmoke/cursors')
    overlay.file('usr/share/lilsmoke/cursors/lilsmoke-cursors.webp',
                 (ROOT/'package/lilsmoke-ui/src/assets/lilsmoke-cursors.webp').read_bytes())
    for name in ('lilsmoke-retroarch','lilsmoke-launch'):
        overlay.file('usr/lib/lilsmoke/'+name,(ROOT/'package/lilsmoke-scripts/src'/name).read_bytes(),0o755)
    overlay.file('etc/init.d/S02mdev',(ROOT/'board/qemu-mipsel/init.d/S02mdev').read_bytes(),0o755)
    overlay.file('etc/retroarch.cfg',(ROOT/'package/retroarch-mips/retroarch.cfg').read_bytes())
    overlay.file('etc/directfbrc',(ROOT/'board/qemu-mipsel/directfbrc').read_bytes())
    overlay.file('usr/lib/libretro/snes9x_2005_libretro.so',a.core.read_bytes())
    overlay.file('usr/share/libretro/info/snes9x_2005_libretro.info',
                 (ROOT/'package/retroarch-mips/core-info/snes9x2005_libretro.info').read_bytes())
    if a.retroarch: overlay.file('usr/bin/retroarch',a.retroarch.read_bytes(),0o755)
    if a.ui: overlay.file('usr/bin/lilsmoke-ui',a.ui.read_bytes(),0o755)
    if a.sdl: overlay.file('usr/lib/libSDL2-2.0.so.0.2800.5',a.sdl.read_bytes(),0o755)
    if a.directfb_input: overlay.file('usr/lib/directfb-1.7-7/inputdrivers/libdirectfb_linux_input.so',a.directfb_input.read_bytes(),0o755)
    if a.trace_input:
        trace=out/'input-trace.so'
        subprocess.run([str(a.build/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-O2','-fPIC','-shared',
            '-I'+str(a.build/'host/mipsel-buildroot-linux-musl/sysroot/usr/include/SDL2'),
            str(ROOT/'tools/hwtest/sdl_input_trace.c'),'-lSDL2','-ldl','-o',str(trace)],check=True)
        overlay.file('usr/lib/input-trace.so',trace.read_bytes(),0o755)
        frontend=a.retroarch or a.build/'target/usr/bin/retroarch'
        overlay.file('usr/bin/retroarch-real',frontend.read_bytes(),0o755)
        overlay.file('usr/bin/retroarch','#!/bin/sh\nexport LD_PRELOAD=/usr/lib/input-trace.so LILSMOKE_TRACE_DFB_KEYS=1\nexec /usr/bin/retroarch-real "$@"\n',0o755)
    overlay.file('etc/init.d/S99qa-report','''#!/bin/sh
case "$1" in start)
{ echo QA_BOOT_STATE; cat /proc/meminfo; cat /proc/sys/kernel/hotplug; ps;
  echo QA_CORE_METADATA; cat /usr/share/libretro/info/snes9x_2005_libretro.info; } >/persist/boot-state.log
;; esac
''',0o755)
    if a.trace_vt:
        overlay.file('etc/init.d/S98qa-vt','''#!/bin/sh
case "$1" in start)
(while :; do cat /sys/class/tty/tty0/active; sleep 2; done) >/persist/vt-state.log 2>&1 &
;; esac
''',0o755)
    initrd=out/'ui-test.cpio.gz'
    with initrd.open('wb') as f:
        f.write((a.build/'images/rootfs.cpio.gz').read_bytes())
        f.write(gzip.compress(overlay.build()))
    sock=out/'qmp.sock'
    command=[a.qemu,'-M','malta','-cpu',a.cpu,'-smp','1',
        '-accel','tcg,thread=single','-m',str(a.ram),
        '-kernel',str(a.build/'images/vmlinux'),'-initrd',str(initrd),
        '-append','console=ttyS0 video=cirrusfb:856x480M-24@60 panic=-1',
        '-vga','cirrus','-display','none','-serial','stdio',
        '-qmp',f'unix:{sock},server=on,wait=off','-no-reboot','-device','usb-kbd',
        '-audiodev','driver=none,id=audio','-device','AC97,audiodev=audio',
        '-virtfs',f'local,path={storage},mount_tag=storage,security_model=none',
        '-virtfs',f'local,path={persist},mount_tag=persist,security_model=none']
    def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
    metadata=dict(command=command,core_sha256=digest(a.core),
        kernel_sha256=digest(a.build/'images/vmlinux'),archive_sha256=digest(a.archive),
        ui_sha256=digest(a.ui) if a.ui else None,
        sdl_sha256=digest(a.sdl) if a.sdl else None,
        directfb_input_sha256=digest(a.directfb_input) if a.directfb_input else None,
        retroarch_sha256=digest(a.retroarch) if a.retroarch else None,
        qemu_sha256=digest(Path(shutil.which(a.qemu) or a.qemu)),
        recording_fps=15 if a.record else None,
        diagnostic_input_trace=a.trace_input,
        diagnostic_vt_trace=a.trace_vt,
        note='Functional MIPS VM test. Not cycle-accurate HC16xx performance. Recording is silent; guest ALSA remains enabled.')
    events=[]
    def qmp(name,arguments=None):
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
            s.settimeout(20); s.connect(str(sock))
            with s.makefile('rwb',buffering=0) as f:
                json.loads(f.readline())
                for cmd in ({'execute':'qmp_capabilities'},dict(execute=name,arguments=arguments or {})):
                    f.write(json.dumps(cmd).encode()+b'\n')
                    while True:
                        reply=json.loads(f.readline())
                        if 'error' in reply: raise RuntimeError(reply)
                        if 'return' in reply: break
                return reply['return']
    started=time.monotonic()
    def key(value,hold=180):
        qmp('human-monitor-command',{'command-line':f'sendkey {value} {hold}'})
        events.append(dict(seconds=time.monotonic()-started,key=value,hold_ms=hold))
        time.sleep(hold/1000+0.3)
    def screenshot(name):
        ppm=out/(name+'.ppm'); png=out/(name+'.png')
        qmp('screendump',{'filename':str(ppm)})
        subprocess.run(['convert',str(ppm),str(png)],check=True)
        return png
    def read(path): return path.read_text(errors='replace') if path.exists() else ''
    def wait_log(path,needle,timeout=150):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if proc.poll() is not None: raise RuntimeError('VM exited')
            if needle in read(path): return
            time.sleep(1)
        raise TimeoutError(needle)
    def automatic_flow():
        key('f8'); time.sleep(3)
        key('ret',800); time.sleep(5); screenshot('04-start-response')
        key('ctrl',600); time.sleep(3)
        key('ctrl',600); time.sleep(25); screenshot('05-intro')
        key('ctrl',600); time.sleep(15); screenshot('05-next-screen')
        key('left',800); time.sleep(5)
        key('ctrl',600); time.sleep(8); screenshot('06-level-entry')
        key('alt-right',1800); time.sleep(3)
        key('ctrl',800); time.sleep(3); screenshot('07-buttons-response')
        key('ret',400); time.sleep(2); screenshot('08-start-pause')
        key('ret',400); time.sleep(2)
        key('f2',500); time.sleep(3)
        key('right',1200); time.sleep(2)
        key('f4',500); time.sleep(3); screenshot('09-state-load')
        key('f1',500); time.sleep(3); screenshot('10-retroarch-menu')
        wait_log(ra,'[State]: Loading state')
        if not list((persist/'states').rglob('*.state')):
            raise RuntimeError('save-state test did not create a state file')
        if not list((persist/'screenshots').glob('*.png')):
            raise RuntimeError('screenshot hotkey did not create a PNG')
        print('UI_SCREENSHOT_OK',flush=True)
        print('UI_FLOW_CAPTURED; inspect screenshots for game/menu state',flush=True)

    ui=persist/'logs/lilsmoke-ui.log'; ra=persist/'logs/retroarch.log'
    xvfb=None; recorder=None
    environment=os.environ.copy()
    if a.record:
        for display_number in range(177,200):
            if not Path(f'/tmp/.X{display_number}-lock').exists(): break
        else: raise RuntimeError('no free isolated X display')
        environment.update(DISPLAY=f':{display_number}',SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy')
        xvfb=subprocess.Popen(['Xvfb',environment['DISPLAY'],'-screen','0','856x480x24','-nolisten','tcp','-ac'],
            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        time.sleep(1)
        if xvfb.poll() is not None: raise RuntimeError('isolated Xvfb failed')
        command[command.index('none')]='sdl,gl=off'
        command+=['-name','LilSmokeOS emulation verification']
    (out/'manifest.json').write_text(json.dumps(metadata,indent=2))
    with (out/'serial.log').open('wb') as serial:
        proc=None
        try:
            proc=subprocess.Popen(command,stdout=serial,stderr=subprocess.STDOUT,env=environment)
            wait_log(ui,'SystemView: first frame complete')
            if a.record:
                metadata['recording_started_wall_seconds']=time.time()
                metadata['live_capture_codec']='ffv1; WebM VP9 encoded after VM shutdown'
                (out/'manifest.json').write_text(json.dumps(metadata,indent=2))
                window=subprocess.check_output(['xdotool','search','--onlyvisible','--name','LilSmokeOS emulation verification'],env=environment,text=True).splitlines()[0]
                subprocess.run(['xdotool','windowmove',window,'0','0'],env=environment,check=True)
                recorder=subprocess.Popen(['ffmpeg','-y','-nostdin','-hide_banner','-loglevel','error',
                    '-f','x11grab','-draw_mouse','0','-framerate','15','-video_size','856x480',
                    '-i',environment['DISPLAY']+'.0+0,0','-an','-c:v','ffv1',
                    '-level','1','-coder','0','-context','0','-threads','1','-pix_fmt','bgr0',
                    str(out/'raw-video.mkv')],env=environment,
                    stdout=subprocess.DEVNULL,stderr=(out/'recording.log').open('wb'))
                time.sleep(2)
                if recorder.poll() is not None: raise RuntimeError('recording failed; inspect recording.log')
            screenshot('01-system-snes'); print('UI_SYSTEM_OK',flush=True)
            key('ctrl'); wait_log(ui,'GameList: ready for snes')
            screenshot('02-game-list'); print('UI_LIST_OK',flush=True)
            key('ctrl'); wait_log(ui,'Launcher: starting')
            wait_log(ra,'[ALSA]: Initialized PLAYBACK device')
            assert 'snes9x_2005_libretro.so' in read(ra)
            print('UI_ARCHIVE_LAUNCH_OK',flush=True)
            deadline=time.monotonic()+240
            while time.monotonic()<deadline:
                image=screenshot('03-game-boot')
                metrics=subprocess.check_output(['identify','-format','%k %[fx:mean]',str(image)]).decode().split()
                colors,mean=int(metrics[0]),float(metrics[1])
                # A cursor and FPS overlay alone can exceed 220 colors. Require
                # substantial screen content before sending game input.
                if colors>220 and mean>0.08: break
                time.sleep(5)
            else: raise TimeoutError('color game frame')
            print('UI_COLORED_GAME_FRAME_OK',flush=True)
            if a.hold:
                print('UI_MANUAL_QMP_READY',flush=True)
                while proc.poll() is None: time.sleep(1)
            else:
                automatic_flow()
        finally:
            (out/'input-events.json').write_text(json.dumps(events,indent=2))
            for process,stop_signal in ((recorder,signal.SIGINT),(proc,signal.SIGTERM),(xvfb,signal.SIGTERM)):
                if process and process.poll() is None:
                    process.send_signal(stop_signal)
                    try: process.wait(timeout=15)
                    except subprocess.TimeoutExpired: process.kill(); process.wait()
    if a.record:
        subprocess.run(['ffmpeg','-y','-nostdin','-hide_banner','-loglevel','error',
            '-i',str(out/'raw-video.mkv'),'-an','-c:v','libvpx-vp9','-deadline','good',
            '-cpu-used','4','-threads','2','-crf','32','-b:v','0',str(out/'lilsmoke-snes-ui.webm')],check=True)
        probe=subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(out/'lilsmoke-snes-ui.webm')],text=True)
        (out/'recording-probe.json').write_text(probe)

if __name__=='__main__': main()
