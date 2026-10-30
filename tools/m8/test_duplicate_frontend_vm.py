"""Real MIPS RetroArch/SDL2/RGUI integration, native MMIO presenter mocked."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
QA=Path('/root/lilsmoke-native-duplicates-20261010')
QA.mkdir(parents=True,exist_ok=True)
Q=Path(tempfile.mkdtemp(prefix='frontend-',dir=QA))
sys.path.insert(0,str(P/'tools/hwtest'))
sys.path.insert(0,str(P/'tools/portsmoke/sm64'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
from vm_scene_smoke import Qmp
cc=B/'host/bin/mipsel-buildroot-linux-musl-gcc'
subprocess.run([str(cc),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(P/'tools/m8/nonblocking_command_launcher.c'),'-o',str(Q/'command-launcher')],check=True)
for source,output in [('duplicate_core.c','core.so'),('mock_native_present.c','present.so')]:
    subprocess.run([str(cc),'-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared',
                    '-I'+str(P/'package/retroarch-mips/src/retroarch/libretro-common/include'),
                    str(P/'tools/m8'/source),'-o',str(Q/output)],check=True)
init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
set -x
mkdir -p /bench /persist /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t tmpfs -o size=32m tmpfs /dev/shm
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_DFB_EVDEV_KEYCODES=1 LILSMOKE_HCFB=1 LILSMOKE_M8_AUDIO=1
export LILSMOKE_SCREEN_WIDTH=856 LILSMOKE_SCREEN_HEIGHT=480
cat /proc/meminfo > /bench/memory.txt
sha256sum /usr/lib/libfusion-1.7.so.7 /usr/lib/libdirect-1.7.so.7 > /bench/directfb-pair.sha256
mkfifo /tmp/ra-commands
(
    exec 3>/tmp/ra-commands
    until grep -q '^QA_CORE_FRAME=100$' /bench/retroarch.log 2>/dev/null; do sleep 1; done
    printf 'MENU_TOGGLE\n' >&3
    until grep -q '^QA_MENU_OPEN$' /bench/retroarch.log; do sleep 1; done
    sleep 1
    printf 'MENU_TOGGLE\n' >&3
    until grep -q '^QA_CACHED_GAME_RESTORED$' /bench/retroarch.log; do sleep 1; done
    touch /bench/continue-core
    until grep -q '^QA_CORE_FRAME=1000$' /bench/retroarch.log; do sleep 1; done
    printf 'QUIT\\n' >&3
) &
LD_PRELOAD=/bench/present.so /bench/command-launcher /usr/bin/retroarch --verbose --config /bench/retroarch.cfg -L /bench/core.so /bench/fixture.test < /tmp/ra-commands > /bench/retroarch.log 2>&1 &
pid=$!
(
    while kill -0 "$pid" 2>/dev/null; do
        sleep 2
        cat /proc/"$pid"/wchan /proc/"$pid"/syscall > /bench/process-diagnostic.txt 2>&1
    done
) &
wait "$pid"
echo QA_RETROARCH_EXIT=$?
cat /bench/retroarch.log
echo QA_FRONTEND_DONE
sync
poweroff -f
'''
(Q/'retroarch.cfg').write_text('''video_driver = "sdl2"
input_driver = "sdl2"
input_joypad_driver = "lilsmoke"
audio_driver = "lilsmoke"
audio_enable = "true"
microphone_enable = "false"
video_fullscreen = "true"
video_vsync = "false"
video_threaded = "false"
video_smooth = "false"
video_font_enable = "false"
aspect_ratio_index = "22"
menu_driver = "rgui"
menu_show_start_screen = "false"
menu_scale_factor = "1.0"
rgui_aspect_ratio = "3"
rgui_internal_upscale_level = "0"
menu_pause_libretro = "true"
pause_nonactive = "false"
input_menu_toggle = "f1"
input_menu_toggle_btn = "16"
input_menu_toggle_gamepad_combo = "0"
savestate_auto_load = "false"
savestate_auto_save = "false"
config_save_on_exit = "false"
quit_on_close_content = "1"
quit_press_twice = "false"
stdin_cmd_enable = "true"
''')
(Q/'fixture.test').write_bytes(b'original disposable duplicate-frame test fixture')
overlay=Newc()
overlay.file('usr/bin/bench-init',init,0o755)
overlay.file('usr/bin/retroarch',(B/'target/usr/bin/retroarch').read_bytes(),0o755)
runtime_libraries(B,overlay,[B/'target/usr/bin/retroarch'])
payload_hashes = {}
dependency_payload = os.environ.get('QA_DIRECTFB_PAYLOAD')
if dependency_payload:
    for name in ('libfusion', 'libdirect'):
        soname = name + '-1.7.so.7'
        payload = Path(dependency_payload) / (soname + '.0.0')
        content = payload.read_bytes()
        payload_hashes['/usr/lib/' + soname] = hashlib.sha256(content).hexdigest()
        overlay.entries = [entry for entry in overlay.entries if entry.name != 'usr/lib/' + soname]
        overlay.file('usr/lib/' + soname, content, 0o755)
overlay.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
initrd=Q/'boot.cpio.gz'
boot=overlay_initramfs(B,overlay,'/usr/bin/bench-init',overlay_mib=32,trace=True,rootfs_in_ram=True)
initrd.write_bytes(gzip.compress(boot))
cmd=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta','-cpu','74Kf-lilsmoke','-smp','1',
     '-m','120','-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),'-append','console=ttyS0 video=cirrusfb:640x480M-16@60 panic=-1',
     '-vga','cirrus','-display','none','-serial','stdio','-monitor','none','-no-reboot','-device','usb-kbd',
     '-qmp',f'unix:{Q}/qmp.sock,server=on,wait=off',*disk_devices(B,[('bench',Q,False)])]
for old in ['retroarch.log','serial.log','qmp.sock','continue-core']:
    (Q/old).unlink(missing_ok=True)
with (Q/'serial.log').open('wb') as log:
    vm=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    qmp=None;stage=0;deadline=time.monotonic()+240
    try:
        while vm.poll() is None:
            if 'reboot: System halted' in (Q/'serial.log').read_text(errors='replace'):break
            if time.monotonic()>deadline:raise RuntimeError('MIPS frontend timeout')
            current=(Q/'retroarch.log').read_text(errors='replace') if (Q/'retroarch.log').exists() else ''
            if 'QA_CACHED_GAME_RESTORED' in current:stage=3
            time.sleep(.2)
    finally:
        if qmp:qmp.close()
        if vm.poll() is None:vm.terminate()
        try:vm.wait(timeout=10)
        except subprocess.TimeoutExpired:vm.kill();vm.wait()
text=(Q/'retroarch.log').read_text(errors='replace')
serial=(Q/'serial.log').read_text(errors='replace')
assert 'QA_RETROARCH_EXIT=0' in serial and 'QA_CORE_DONE frames=1000' in text,serial[-3000:]
match=re.search(r'QA_PRESENT_RESULT games=(\d+) menus=(\d+) resets=(\d+) restored=(\d+)',text)
assert match,text[-3000:]
games,menus,resets,restored=map(int,match.groups())
assert stage==3 and 9<=games<=11 and menus>0 and resets==2 and restored==1,match.group()
assert text.index('QA_CORE_FRAME=100\n') < text.index('QA_NATIVE_RESET=1\n') < text.index('QA_CACHED_GAME_RESTORED\n')
assert text.index('QA_CORE_FRAME=1000\n') < text.index('QA_NATIVE_RESET=2\n')
guest_hashes = {}
for line in (Q / 'directfb-pair.sha256').read_text().splitlines():
    digest, name = line.split(None, 1)
    guest_hashes[name] = digest
for name, digest in payload_hashes.items():
    assert guest_hashes.get(name) == digest, 'Wrong DirectFB library in guest: ' + name
(Q/'report.json').write_text(json.dumps(dict(passed=True,frames=1000,game_presentations=games,menu_presentations=menus,
    resets=resets,owned_frame_restores=restored,guest_ram_mib=120,cpus=1,hardware_3d=False,
    native_mmio_mocked=True,rootfs_backing='read-only SDK SquashFS in guest RAM',
    menu_control='in-guest RetroArch stdin command API', dependency_payload=dependency_payload,
    directfb_guest_hashes=guest_hashes),indent=2)+'\n')
print('PASS actual MIPS RetroArch frontend: 1000 frames, duplicate-frame bypass, RGUI open/return through the in-guest command API, owned game restored, resets only for menu transition and teardown; 120 MiB guest, no GPU. Native MMIO is mocked, not physical FPS.')
print('Evidence:',Q)
