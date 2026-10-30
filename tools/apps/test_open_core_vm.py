"""Actual MIPS Settings view -> saved app choice -> production ROM router."""
import gzip
import json
from pathlib import Path
import subprocess
import sys
import time

P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
Q=Path('/root/lilsmoke-open-core-20261010/settings-vm')
Q.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(P/'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
S=P/'package/lilsmoke-ui/src/src'
sources=[P/'tools/apps/open_core_ui_test.cpp',*[S/(name+'.cpp') for name in
         ['EmulatorSettingsView','Config','Renderer','Font','Log','Theme']]]
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-g++'),'-std=c++17','-O2','-Wall','-Wextra',
                '-I'+str(S),'-I'+str(P/'package/lilsmoke-scripts/src'),'-I'+str(B/'staging/usr/include/SDL2'),
                *map(str,sources),'-lSDL2_image','-lSDL2_ttf','-lSDL2','-lz','-ljson-c','-lwebp','-o',str(Q/'ui-test')],check=True)
init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /persist /dev/pts /dev/shm /storage/roms/gba /storage/independent-apps/VM-Emulator /tmp/no-providers
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t tmpfs -o size=8m tmpfs /dev/shm
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_HCFB=0 LILSMOKE_CORES_DIR=/tmp/no-providers LILSMOKE_OD_EMULATORS_DIR=/tmp/no-providers
export LILSMOKE_EXTRA_CORES_DIR=/tmp/no-providers LILSMOKE_EXTRA_OD_DIR=/tmp/no-providers LILSMOKE_OPENDINGUX_HELPER=/bin/true
export LILSMOKE_APPS_HELPER=/usr/lib/lilsmoke/lilsmoke-apps LILSMOKE_SYSTEM_CONFIG=/bench/config
cat /proc/meminfo > /bench/memory.txt
printf 'EMULATOR_BACKEND=retroarch\n' > /bench/config
printf 'archive fixture: must never reach default extractor' > '/storage/roms/gba/Game with spaces.7z'
/bench/ui-test /bench/config /usr/share/fonts/dejavu/DejaVuSans.ttf /bench/settings.bmp || { echo QA_UI_FAILED; poweroff -f; }
/usr/lib/lilsmoke/lilsmoke-launch gba '/storage/roms/gba/Game with spaces.7z'
echo QA_ROUTER_EXIT=$?
cat /bench/app-result.txt
echo QA_OPEN_CORE_VM_DONE
sync
poweroff -f
'''
overlay=Newc()
overlay.file('usr/bin/bench-init',init,0o755)
overlay.file('storage/independent-apps/VM-Emulator/meta.json',json.dumps(dict(name='VM-Emulator',id='org.test.vm-emulator',version='1.0.0',open_core=['gba','md'])))
overlay.file('storage/independent-apps/VM-Emulator/run.sh','''#!/bin/sh
set -eu
printf '%s\\n' "$core" "$rom" "$#" "$@" > /bench/app-result.txt
''',0o755)
for name in ['lilsmoke-apps','lilsmoke-emulators','lilsmoke-launch']:
    source=B/('target/usr/lib/lilsmoke/'+name)
    overlay.file('usr/lib/lilsmoke/'+name,source.read_bytes(),0o755)
runtime_libraries(B,overlay,[Q/'ui-test',B/'target/usr/lib/lilsmoke/lilsmoke-apps',B/'target/usr/lib/lilsmoke/lilsmoke-emulators'])
overlay.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
initrd=Q/'boot.cpio.gz';initrd.write_bytes(gzip.compress(overlay_initramfs(B,overlay,'/usr/bin/bench-init',overlay_mib=32)))
cmd=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta','-cpu','74Kf-lilsmoke','-smp','1',
     '-m','120','-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),'-append','console=ttyS0 video=cirrusfb:800x450M-16@60 panic=-1',
     '-vga','cirrus','-display','none','-serial','stdio','-monitor','none','-no-reboot',*disk_devices(B,[('bench',Q,False)])]
with (Q/'serial.log').open('wb') as log:
    vm=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    try:
        deadline=time.monotonic()+150
        while 'reboot: System halted' not in (Q/'serial.log').read_text(errors='replace'):
            if vm.poll() is not None or time.monotonic()>deadline:raise RuntimeError('MIPS settings guest did not finish')
            time.sleep(.25)
    finally:
        if vm.poll() is None:vm.terminate()
        try:vm.wait(timeout=10)
        except subprocess.TimeoutExpired:vm.kill();vm.wait()
serial=(Q/'serial.log').read_text(errors='replace')
assert 'QA_APP_SELECTED_IN_REAL_SETTINGS' in serial and 'QA_ROUTER_EXIT=0' in serial,serial[-3000:]
assert (Q/'app-result.txt').read_text().splitlines()==['gba','Game with spaces.7z','2','core=gba','rom=Game with spaces.7z']
assert '/storage/roms' not in (Q/'app-result.txt').read_text()
from PIL import Image
Image.open(Q/'settings.bmp').save(Q/'settings.png')
print('PASS actual MIPS Settings choice saved -> production lilsmoke-launch -> router -> bundled root script; original 7z filename, two quoted arguments, no absolute ROM path; 120 MiB CPU-only Linux guest.')
