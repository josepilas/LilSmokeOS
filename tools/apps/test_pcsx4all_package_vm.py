"""Actual bundled PCSX4ALL menu and ROM launch in a private CPU-only MIPS OS."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from PIL import Image

P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--rom',type=Path)
p.add_argument('--payload',type=Path,required=True,help='actual generated SD runtime, not remapped SDK binaries')
p.add_argument('--full-ui',action='store_true',help='also drive the production home/apps/game-list UI via private QMP')
p.add_argument('--version',default='1.0.1')
a=p.parse_args();Q=a.out.resolve();Q.mkdir(parents=True,exist_ok=False)
assert not a.full_ui or a.rom,'Full UI launch requires real game content'
assert len(str(Q/'qmp.sock').encode())<100
sys.path.insert(0,str(P/'tools/hwtest'))
sys.path.insert(0,str(P/'tools/portsmoke/sm64'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
from vm_scene_smoke import Qmp
storage=Q/'storage';(storage/'roms/psx').mkdir(parents=True)
if a.rom:shutil.copyfile(a.rom,storage/'roms/psx/Crash-test.zip')
init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /storage /persist /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L storage /storage || poweroff -f
mount -t tmpfs -o size=32m tmpfs /dev/shm
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_DFB_EVDEV_KEYCODES=1 LILSMOKE_HCFB=0 LILSMOKE_AUDIO_OVERRIDE=dummy
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export LILSMOKE_LOG_DIR=/bench
cat /proc/meminfo > /bench/memory.txt
/etc/init.d/S08applications start || { echo QA_SEED_FAILED; poweroff -f; }
/usr/lib/lilsmoke/lilsmoke-apps emulators > /bench/providers.txt
printf 'EMULATOR_psx=od:pcsx4all\n' > /bench/config
export LILSMOKE_SYSTEM_CONFIG=/bench/config
/usr/lib/lilsmoke/lilsmoke-emulators resolve psx > /bench/selection.txt
echo QA_APP_READY
export LILSMOKE_BENCH_FRAMES=600 LILSMOKE_BENCH_GAMEPLAY=1
/usr/lib/lilsmoke/lilsmoke-apps run PCSX4ALL
echo QA_OWN_MENU_EXIT=$?
if [ -f /storage/roms/psx/Crash-test.zip ]; then
    printf 'EMULATOR_psx=app:org.lilsmoke.pcsx4all\n' > /bench/config
    echo QA_HOME_LAUNCH
    /usr/lib/lilsmoke/lilsmoke-launch psx /storage/roms/psx/Crash-test.zip
    echo QA_GAME_EXIT=$?
fi
cat /bench/pcsx4all-app.log > /bench/pcsx4all.log
cat /bench/pcsx4all.log
sync
poweroff -f
'''
if a.full_ui:
    init=init.replace('cat /bench/pcsx4all-app.log >',"""mkdir -p /persist/config
printf 'DISPLAY_WIDTH=856\\nDISPLAY_HEIGHT=480\\nPOINTER_ENABLED=off\\nEMULATOR_BACKEND=retroarch\\nEMULATOR_psx=app:org.lilsmoke.pcsx4all\\nSCAN_ROMS_ON_BOOT=on\\n' > /persist/config/lilsmoke.conf
echo QA_UI_HOME
/usr/bin/lilsmoke-ui > /bench/ui.log 2>&1
echo QA_UI_EXIT=$?
cat /bench/ui.log
cat /bench/pcsx4all-app.log >""")
overlay=Newc();overlay.file('usr/bin/bench-init',init,0o755)
rows={row[2]:row for row in (line.split('\t') for line in (a.payload/'runtime.tsv').read_text().splitlines())}
def shipped(path):
    row=rows[path]
    assert row[0] in ('F','C'),path
    source=a.payload/'files'/row[3]
    assert source.read_bytes()==(B/'target'/path).read_bytes(),'SDK and payload disagree: '+path
    overlay.file(path,source.read_bytes(),int(row[1],8))
for name in ['lilsmoke-launch','lilsmoke-emulators']:
    row=rows['usr/bin/'+name]
    assert row[0]=='L' and row[3]=='../lib/lilsmoke/'+name,'Diverging launcher aliases'
    overlay.symlink(row[2],row[3])
for name in ['lilsmoke-apps','lilsmoke-emulators','lilsmoke-launch','lilsmoke-rom','libod-sdl.so']:
    shipped('usr/lib/lilsmoke/'+name)
shipped('etc/init.d/S08applications')
shipped('usr/share/lilsmoke/builtin-apps/PCSX4ALL-'+a.version+'.lilpck')
if a.full_ui:
    shipped('usr/bin/lilsmoke-ui')
    runtime_libraries(B,overlay,[B/'target/usr/bin/lilsmoke-ui'])
overlay.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
runtime_libraries(B,overlay,[B/'target/usr/lib/lilsmoke/lilsmoke-apps',B/'target/usr/lib/lilsmoke/lilsmoke-emulators',B/'target/usr/lib/lilsmoke/lilsmoke-rom',B/'target/usr/lib/lilsmoke/libod-sdl.so'])
initrd=Q/'boot.cpio.gz';initrd.write_bytes(gzip.compress(overlay_initramfs(B,overlay,'/usr/bin/bench-init',overlay_mib=32,rootfs_in_ram=True)))
cmd=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','120',
     '-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),'-append','console=ttyS0 video=cirrusfb:856x480M-16@60 panic=-1',
     '-vga','cirrus','-display','none','-serial','stdio','-monitor','none','-no-reboot','-device','usb-kbd',
     '-qmp',f'unix:{Q}/qmp.sock,server=on,wait=off',*disk_devices(B,[('bench',Q,False),('storage',storage,False)])]
with (Q/'serial.log').open('wb') as log:
    vm=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    qmp=None;stage=0;deadline=time.monotonic()+600
    try:
        while vm.poll() is None:
            if time.monotonic()>deadline:raise RuntimeError('PCSX4ALL bundled-app guest timed out')
            serial=(Q/'serial.log').read_text(errors='replace')
            if 'reboot: System halted' in serial:break
            if qmp is None and (Q/'qmp.sock').exists():
                try:qmp=Qmp(Q/'qmp.sock')
                except ConnectionRefusedError:time.sleep(.1);continue
            native_log=(Q/'pcsx4all-app.log').read_text(errors='replace') if (Q/'pcsx4all-app.log').exists() else ''
            if stage==0 and qmp and 'PCSX_OWN_MENU_ENTER' in native_log:
                time.sleep(.5)
                qmp.call('screendump',dict(filename=str(Q/'own-menu.ppm')))
                if a.rom:
                    qmp.key('ctrl');time.sleep(.7)
                    qmp.key('down');time.sleep(.5)
                    qmp.key('ctrl');stage=1
                else:
                    qmp.key('alt');stage=1
            elif stage==1 and 'PCSX_VIDEO frames=' in native_log:
                qmp.call('screendump',dict(filename=str(Q/'game.ppm')));stage=2
            elif a.full_ui and stage==2 and 'QA_UI_HOME' in serial and (Q/'ui.log').exists() and 'UI: entering main loop' in (Q/'ui.log').read_text(errors='replace'):
                time.sleep(1)
                qmp.call('screendump',dict(filename=str(Q/'home.ppm')))
                qmp.key('right');time.sleep(.6);qmp.key('ret');time.sleep(2)
                qmp.call('screendump',dict(filename=str(Q/'applications-icon.ppm')))
                qmp.key('alt');time.sleep(.6);qmp.key('left');time.sleep(.6)
                qmp.key('ret');time.sleep(2)
                qmp.call('screendump',dict(filename=str(Q/'game-list.ppm')))
                qmp.key('ret');stage=3
            time.sleep(.25)
    finally:
        if qmp:qmp.close()
        if vm.poll() is None:vm.terminate()
        try:vm.wait(timeout=10)
        except subprocess.TimeoutExpired:vm.kill();vm.wait()
serial=(Q/'serial.log').read_text(errors='replace')
assert 'QA_OWN_MENU_EXIT=0' in serial,serial[-2500:]
assert (Q/'selection.txt').read_text().strip()=='app:org.lilsmoke.pcsx4all'
assert 'org.lilsmoke.pcsx4all\tPCSX4ALL\tpsx\t'+a.version in (Q/'providers.txt').read_text()
if a.rom:
    assert 'QA_GAME_EXIT=0' in serial and 'PCSX4ALL_APP_LAUNCH menu=0' in (Q/'pcsx4all.log').read_text(),serial[-2500:]
    assert 'PCSX_VIDEO frames=' in serial
    native_log=(Q/'pcsx4all.log').read_text()
    expected=3 if a.full_ui else 2
    assert 'PCSX_OWN_MENU_GAME_HANDOFF' in native_log and native_log.count('PCSX_FRONTEND_STOP frames=600')==expected
    assert native_log.count('PCSX4ALL_APP_LAUNCH menu=0')==expected
    assert 'Unknown native engine' not in native_log
if a.full_ui:
    ui=(Q/'ui.log').read_text(errors='replace')
    assert stage==3 and 'QA_UI_EXIT=0' in serial and 'Applications: catalog ready (1)' in ui
    assert 'Launcher: starting /storage/roms/psx/Crash-test.zip [psx]' in ui
for file in Q.glob('*.ppm'):Image.open(file).save(file.with_suffix('.png'))
manifest=dict(passed=True,cpus=1,guest_ram_mib=120,gpu3d=False,software_framebuffer=True,
              builtin_seed_passed=True,own_application_menu=True,legacy_provider_migrated=True,
              zip_game_launched=bool(a.rom),core_engine_unchanged=True,hardware_fps_not_measured=True)
manifest.update(actual_sd_payload=True,own_menu_game_handoff=bool(a.rom),home_screen_router_launch=bool(a.rom))
manifest['production_ui_navigation']=a.full_ui
(Q/'report.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
