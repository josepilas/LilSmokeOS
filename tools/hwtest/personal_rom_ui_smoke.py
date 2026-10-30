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
    def terminate(signum, frame):
        raise SystemExit(128+signum)
    signal.signal(signal.SIGTERM, terminate)
    p=argparse.ArgumentParser()
    p.add_argument('--build',type=Path,default=Path('/root/lilsmoke-os/build/buildroot/output'))
    p.add_argument('--qemu',default='qemu-system-mipsel')
    p.add_argument('--cpu',choices=['74Kf','74Kf-lilsmoke'],default='74Kf')
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--storage',type=Path,help='reuse an isolated test storage and validated archive cache')
    p.add_argument('--core',type=Path,required=True,help='MIPS libretro core (N64) or pcsx4all-launch (PSX)')
    p.add_argument('--system',choices=['psx','n64','snes'],required=True)
    p.add_argument('--snes-backend',choices=['retroarch','pocketsnes'],default='retroarch')
    p.add_argument('--debug-core',action='store_true',help='isolated PocketSNES core dump for diagnosis, not performance evidence')
    p.add_argument('--retroarch',type=Path)
    p.add_argument('--ui',type=Path)
    p.add_argument('--sdl',type=Path)
    p.add_argument('--directfb-input',type=Path)
    p.add_argument('--record',action='store_true',help='15 FPS silent WebM of the actual VM display; not a timing benchmark')
    p.add_argument('--trace-input',action='store_true',help='diagnostic SDL key-state trace; invalidates performance comparisons')
    p.add_argument('--input-probe',action='store_true',help='SDL input diagnostic instead of a game; never emulation evidence')
    p.add_argument('--trace-vt',action='store_true',help='log active Linux terminal during functional keyboard tests; not a timing run')
    p.add_argument('--hold',action='store_true',help='after game boot, leave the VM running for manual QMP-only input until QMP quit')
    p.add_argument('--ram',type=int,choices=[128,256],default=256)
    p.add_argument('--disk-root',action='store_true',help='boot the complete built SquashFS from a read-only block device, not a RAM-resident rootfs')
    a=p.parse_args()
    if a.input_probe and (a.system!='n64' or a.trace_input or a.record):
        p.error('input probe requires n64 routing, without trace interposition or game recording')
    if a.disk_root and (a.trace_input or a.input_probe or a.trace_vt or a.debug_core or a.ui or a.sdl or a.retroarch or a.directfb_input):
        p.error('disk-root runs the built OS without diagnostic/package overrides')
    if a.disk_root:
        installed={'psx':'usr/bin/pcsx4all-launch','n64':'usr/lib/libretro/parallel_n64_libretro.so',
                   'snes':'usr/lib/lilsmoke/od-emulators/pocketsnes' if a.snes_backend=='pocketsnes' else 'usr/lib/libretro/snes9x2005_libretro.so'}[a.system]
        if a.core.read_bytes()!=(a.build/'target'/installed).read_bytes():
            p.error('disk-root requires the installed core, not an experimental replacement')
    out=a.out.resolve()
    out.mkdir(parents=True,exist_ok=False)
    storage=a.storage.resolve() if a.storage else out/'storage'; persist=out/'persist'
    (storage/('roms/'+a.system)).mkdir(parents=True,exist_ok=bool(a.storage))
    (persist/'config').mkdir(parents=True)
    destination=storage/('roms/'+a.system)/a.archive.name
    if a.storage and destination.exists():
        if hashlib.sha256(destination.read_bytes()).digest()!=hashlib.sha256(a.archive.read_bytes()).digest():
            raise RuntimeError('reused storage ROM differs from supplied dump')
    else: shutil.copyfile(a.archive,destination)
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
    if a.system=='snes':
        with (persist/'config/lilsmoke.conf').open('a') as config:
            config.write('SNES_EMULATOR='+a.snes_backend+'\n')
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
    overlay.file('etc/init.d/S03shm',(ROOT/'package/lilsmoke-scripts/src/S03shm').read_bytes(),0o755)
    overlay.file('etc/retroarch.cfg',(ROOT/'package/retroarch-mips/retroarch.cfg').read_bytes())
    overlay.file('etc/directfbrc',(ROOT/'board/qemu-mipsel/directfbrc').read_bytes())
    if a.system=='n64':
        overlay.file('usr/lib/libretro/parallel_n64_libretro.so',a.core.read_bytes())
        overlay.file('usr/share/libretro/info/parallel_n64_libretro.info',
                     (ROOT/'package/lilsmoke-n64/parallel_n64_libretro.info').read_bytes())
    elif a.system=='psx':
        overlay.file('usr/bin/pcsx4all-launch',a.core.read_bytes(),0o755)
        overlay.file('etc/pcsx4all.cfg',(ROOT/'package/pcsx4all/pcsx4all.cfg').read_bytes())
    elif a.snes_backend=='pocketsnes':
        overlay.dir('usr/lib/lilsmoke/od-emulators')
        overlay.dir('usr/share/lilsmoke/pocketsnes')
        overlay.file('usr/lib/lilsmoke/od-emulators/pocketsnes',a.core.read_bytes(),0o755)
        od=(ROOT/'package/opendingux-emulators/src/od-launch').read_text()
        if a.debug_core:
            od=od.replace('set -eu','set -eu\nulimit -c unlimited\necho /persist/pocket.core >/proc/sys/kernel/core_pattern',1)
        overlay.file('usr/lib/lilsmoke/od-launch',od,0o755)
        overlay.file('usr/share/lilsmoke/pocketsnes/backdrop.png',
                     (a.build/'target/usr/share/lilsmoke/pocketsnes/backdrop.png').read_bytes())
        overlay.file('usr/lib/libSDL-1.2.so.0',
                     (a.build/'target/usr/lib/libSDL-1.2.so.0').read_bytes(),0o755)
    else:
        overlay.file('usr/lib/libretro/snes9x2005_libretro.so',a.core.read_bytes())
    if (a.build/'target/usr/bin/lilsmoke-keyboard').is_file():
        overlay.file('usr/bin/lilsmoke-keyboard',(a.build/'target/usr/bin/lilsmoke-keyboard').read_bytes(),0o755)
    if a.retroarch: overlay.file('usr/bin/retroarch',a.retroarch.read_bytes(),0o755)
    if a.input_probe:
        probe=out/'directfb-key-probe'
        subprocess.run([str(a.build/'host/bin/mipsel-buildroot-linux-musl-gcc'),
            '-O2','-I'+str(a.build/'host/mipsel-buildroot-linux-musl/sysroot/usr/include/SDL2'),
            str(ROOT/'tools/hwtest/directfb_key_probe.c'),'-lSDL2','-o',str(probe)],check=True)
        overlay.file('usr/bin/retroarch',probe.read_bytes(),0o755)
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
  echo QA_CORE_METADATA; ls -l /usr/bin/pcsx4all-launch /usr/lib/libretro/parallel_n64_libretro.so; } >/persist/boot-state.log
;; esac
''',0o755)
    if a.trace_vt:
        overlay.file('etc/init.d/S98qa-vt','''#!/bin/sh
case "$1" in start)
(while :; do cat /sys/class/tty/tty0/active; sleep 2; done) >/persist/vt-state.log 2>&1 &
;; esac
''',0o755)
    initrd=out/'ui-test.cpio.gz'
    if not a.disk_root:
        with initrd.open('wb') as f:
            f.write((a.build/'images/rootfs.cpio.gz').read_bytes())
            f.write(gzip.compress(overlay.build()))
    sock=out/'qmp.sock'
    command=[a.qemu,'-M','malta','-cpu',a.cpu,'-smp','1',
        '-accel','tcg,thread=single','-m',str(a.ram),
        '-kernel',str(a.build/'images/vmlinux'),
        '-append','console=ttyS0 video=cirrusfb:856x480M-24@60 panic=-1'+
            (' root=/dev/vda rootfstype=squashfs ro rootwait' if a.disk_root else ''),
        '-vga','cirrus','-display','none','-serial','stdio',
        '-qmp',f'unix:{sock},server=on,wait=off','-no-reboot','-device','usb-kbd',
        '-audiodev','driver=none,id=audio','-device','AC97,audiodev=audio']
    if a.disk_root:
        # Malta routes interrupts for only a small set of PCI slots. Keep the
        # three storage functions in one valid slot instead of auto slot 22.
        command+=['-fsdev',f'local,id=s0,path={storage},security_model=none',
                  '-device','virtio-9p-pci,fsdev=s0,mount_tag=storage,addr=0x14.0,multifunction=on',
                  '-fsdev',f'local,id=p0,path={persist},security_model=none',
                  '-device','virtio-9p-pci,fsdev=p0,mount_tag=persist,addr=0x14.1',
                  '-drive',f'file={a.build / "images/rootfs.squashfs"},if=none,id=osroot,format=raw,readonly=on',
                  '-device','virtio-blk-pci,drive=osroot,addr=0x14.2']
    else:
        command+=['-initrd',str(initrd),
                  '-virtfs',f'local,path={storage},mount_tag=storage,security_model=none',
                  '-virtfs',f'local,path={persist},mount_tag=persist,security_model=none']
    def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
    metadata=dict(command=command,core_sha256=digest(a.core),
        kernel_sha256=digest(a.build/'images/vmlinux'),archive_sha256=digest(a.archive),
        base_rootfs_sha256=digest(a.build/('images/rootfs.squashfs' if a.disk_root else 'images/rootfs.cpio.gz')),
        test_initrd_sha256=None if a.disk_root else digest(initrd),
        rootfs_mode='read-only block SquashFS' if a.disk_root else 'RAM initramfs with package overrides',
        ui_sha256=digest(a.ui) if a.ui else None,
        sdl_sha256=digest(a.sdl) if a.sdl else None,
        directfb_input_sha256=digest(a.directfb_input) if a.directfb_input else None,
        retroarch_sha256=digest(a.retroarch) if a.retroarch else None,
        qemu_sha256=digest(Path(shutil.which(a.qemu) or a.qemu)),
        recording_fps=15 if a.record else None,
        diagnostic_input_trace=a.trace_input,
        diagnostic_input_probe=a.input_probe,
        diagnostic_vt_trace=a.trace_vt,
        diagnostic_core_dump=a.debug_core,
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
    def wait_log(path,needle,timeout=900):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if proc.poll() is not None: raise RuntimeError('VM exited')
            if needle in read(path): return
            time.sleep(1)
        raise TimeoutError(needle)
    def automatic_flow():
        key('ret',700); time.sleep(20); screenshot('04-start')
        for i in range(3):
            key('ret',600); time.sleep(12)
            key('ctrl' if a.system=='psx' else 'x',600); time.sleep(12)
            screenshot(f'05-selection-{i+1}')
        for i in range(3):
            key('right',1400); key('ctrl' if a.system=='psx' else 'x',700)
            key('left',1400); time.sleep(8)
            screenshot(f'06-buttons-{i+1}')
        key('ret',600); time.sleep(5); screenshot('07-start-response')
        print('UI_FLOW_CAPTURED; inspect screenshots for actual game state',flush=True)

    ui=persist/'logs/lilsmoke-ui.log'
    ra=persist/'logs'/('pcsx4all.log' if a.system=='psx' else
                      'pocketsnes.log' if a.system=='snes' and a.snes_backend=='pocketsnes' else 'retroarch.log')
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
            proc=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=serial,stderr=subprocess.STDOUT,env=environment)
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
            screenshot('01-system-'+a.system); print('UI_SYSTEM_OK',flush=True)
            key('ctrl'); wait_log(ui,'GameList: ready for '+a.system)
            screenshot('02-game-list'); print('UI_LIST_OK',flush=True)
            key('ctrl'); wait_log(ui,'Launcher: starting')
            wait_log(ra,'INPUT_PROBE_READY' if a.input_probe else 'PCSX_FRONTEND_READY' if a.system=='psx' else
                     'POCKETSNES_FRAME_READY' if a.system=='snes' and a.snes_backend=='pocketsnes' else '[ALSA]: Initialized PLAYBACK device')
            if a.system=='n64': assert 'parallel_n64_libretro.so' in read(ra)
            print('INPUT_DIAGNOSTIC_READY' if a.input_probe else 'UI_ARCHIVE_LAUNCH_OK',flush=True)
            deadline=time.monotonic()+900
            boots=read(ui).count('LilSmokeUI v0.1 starting')
            while time.monotonic()<deadline:
                if read(ui).count('LilSmokeUI v0.1 starting')>boots:
                    raise RuntimeError('game returned to the UI before a verified frame')
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
                while proc.poll() is None:
                    if recorder and recorder.poll() is not None:
                        raise RuntimeError('recording stopped before the VM; inspect recording.log')
                    time.sleep(1)
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
            '-cpu-used','8','-threads','2','-crf','32','-b:v','0',str(out/('lilsmoke-'+a.system+'-ui.webm'))],check=True)
        probe=subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(out/('lilsmoke-'+a.system+'-ui.webm'))],text=True)
        (out/'recording-probe.json').write_text(probe)

if __name__=='__main__': main()
