"""Actual MIPS RA frontend/core callbacks with explicitly synthetic IPC input."""
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
Q=Path(tempfile.mkdtemp(prefix='ra-analog-',dir='/root'))
print('Private MIPS callback fixture:',Q,flush=True)
cc=B/'host/bin/mipsel-buildroot-linux-musl-gcc'
subprocess.run([str(cc),'-O2','-std=gnu11','-shared','-fPIC','-I'+str(B/'build/retroarch-mips-v1.19.1/libretro-common/include'),
    str(P/'tools/gamepads/retroarch_analog_core.c'),'-o',str(Q/'qa_libretro.so')],check=True)
subprocess.run([str(cc),'-O2','-std=gnu11','-I'+str(P/'package/lilsmoke-ui/src/src'),
    str(P/'tools/gamepads/retroarch_analog_fixture.c'),'-o',str(Q/'producer')],check=True)
subprocess.run([str(cc),'-O2','-std=gnu11','-shared','-fPIC','-Wall','-Wextra','-Werror',
    str(P/'tools/gamepads/qa_fb_present.c'),'-o',str(Q/'present.so')],check=True)
sys.path.insert(0,str(P/'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import disk_devices,runtime_libraries
init='''#!/bin/sh
set -x
echo QA_BENCH_ENTER
mkdir -p /bench /run /persist /dev/pts /dev/shm /run/cores /run/no-native-input
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
printf 'DISPLAY_WIDTH=320\nDISPLAY_HEIGHT=240\n' > /run/system.conf
printf 'synthetic' > /run/Game.gba
export LILSMOKE_HCFB=1 LILSMOKE_DFB_PIXEL_OPTIONS=depth=16,pixelformat=RGB16,system=dummy
export LILSMOKE_INPUT_DIR=/run/no-native-input
export LILSMOKE_CONFIG=/run/system.conf LILSMOKE_CORES_DIR=/run/cores
export LILSMOKE_RETROARCH_USER_CONFIG=/run/user.cfg LILSMOKE_RETROARCH_CORE_OPTIONS=/run/core.cfg
export LILSMOKE_RETROARCH_RUN_DIR=/run/retroarch LILSMOKE_LOG_DIR=/run/logs
export LILSMOKE_SAVE_ROOT=/run/saves LILSMOKE_STATE_ROOT=/run/states
export LILSMOKE_RETROARCH_AUTOCONFIG=/run/autoconfig
export LILSMOKE_ROM_RESOLVER=/usr/bin/qa-resolver
export LD_PRELOAD=/bench/present.so
(
    while :; do
        [ ! -f /run/logs/retroarch.log ] || cp /run/logs/retroarch.log /bench/current-retroarch.log
        ps w >/bench/processes.txt
        for file in /proc/[0-9]*/comm; do
            read -r name < "$file" || continue
            [ "$name" = retroarch ] || continue
            cat "${file%/comm}/status" "${file%/comm}/wchan" "${file%/comm}/syscall" "${file%/comm}/stack"
        done >/bench/process-diagnostic.txt 2>&1
        sleep 1
    done
) &
diagnostic=$!
/usr/bin/qa-analog-producer &
producer=$!
for mode in analog digital; do
    if [ "$mode" = digital ]; then export QA_DIGITAL_ONLY=1; else unset QA_DIGITAL_ONLY; fi
    /bin/busybox sh -x /usr/lib/lilsmoke/lilsmoke-retroarch run gba /run/Game.gba qa_libretro.so
    result=$?
    echo "$result" > /bench/$mode.exit
    echo "QA_RA_EXIT mode=$mode status=$result"
    cat /run/logs/retroarch.log
    cp /run/logs/retroarch.log /bench/$mode.log
    cp /run/retroarch/retroarch.cfg /bench/$mode.cfg
done
kill "$producer" "$diagnostic"
wait "$producer"
wait "$diagnostic"
sync
poweroff -f
'''
overlay=Newc();overlay.file('usr/bin/bench-init',init,0o755)
overlay.file('usr/bin/qa-resolver','#!/bin/sh\nprintf "%s\\n" "$2"\n',0o755)
overlay.file('usr/bin/qa-analog-producer',(Q/'producer').read_bytes(),0o755)
overlay.file('run/cores/qa_libretro.so',(Q/'qa_libretro.so').read_bytes(),0o755)
ra=B/'target/usr/bin/retroarch'
overlay.file('usr/bin/retroarch-real',ra.read_bytes(),0o755)
overlay.file('usr/bin/retroarch','''#!/bin/sh
set -e
# This input-only fixture has no physical auddec/ALSA device or window focus.
# Avoid a driver-error dialog pausing the core before its input callbacks run.
# RetroArch 1.19 keeps the FIRST definition, so private overrides must lead.
config=/run/retroarch/retroarch.cfg
{
    printf '%s\\n' 'audio_enable = false' 'pause_nonactive = false' \\
        'menu_pause_libretro = false' 'menu_show_start_screen = false' \\
        'video_vsync = false' 'quit_on_close_content = 1' \\
        'load_dummy_on_core_shutdown = false'
    cat "$config"
} > "$config.private"
mv "$config.private" "$config"
exec /usr/bin/retroarch-real "$@"
''',0o755)
overlay.file('usr/lib/lilsmoke/lilsmoke-retroarch',(P/'package/lilsmoke-scripts/src/lilsmoke-retroarch').read_bytes(),0o755)
overlay.file('etc/retroarch.cfg',(P/'package/retroarch-mips/retroarch.cfg').read_bytes())
overlay.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
modules=list((B/'target/usr/lib/directfb-1.7-7').rglob('*.so'))
for module in modules:
    overlay.file(str(module.relative_to(B/'target')),module.read_bytes(),0o755)
runtime_libraries(B,overlay,[ra,Q/'producer',Q/'qa_libretro.so',*modules])
for name in ('bin/busybox','lib/libc.so','lib/ld-musl-mipsel.so.1'):
    overlay.file(name,(B/'target'/name).read_bytes(),0o755)
applets=subprocess.check_output(['busybox','--list'],text=True).splitlines()
for name in applets:
    overlay.symlink('bin/'+name,'busybox')
overlay.file('usr/share/fonts/dejavu/DejaVuSans.ttf',(B/'target/usr/share/fonts/dejavu/DejaVuSans.ttf').read_bytes())
overlay.file('init','''#!/bin/sh
set -x
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs dev /dev
export PATH=/bin:/usr/bin:/usr/lib/lilsmoke
'''+init.split('\n',1)[1],0o755)
root=Newc()
parents={str(parent) for entry in overlay.entries for parent in Path(entry.name).parents if str(parent)!='.'}
parents.update(('dev','proc','sys','tmp','run','persist'))
for name in sorted(parents,key=lambda name:(name.count('/'),name)):root.dir(name)
root.chrdev('dev/console',5,1)
root.entries+=overlay.entries
(Q/'boot.cpio.gz').write_bytes(gzip.compress(root.build()))
cmd=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','120',
    '-kernel',str(B/'images/vmlinux'),'-initrd',str(Q/'boot.cpio.gz'),'-append','console=ttyS0 panic=-1',
    '-vga','none','-display','none','-serial','stdio','-monitor','none','-no-reboot',*disk_devices(B,[('bench',Q,False)])]
(Q/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
with (Q/'serial.log').open('wb') as log:
    vm=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    try:
        deadline=time.monotonic()+240
        while vm.poll() is None and 'reboot: System halted' not in (Q/'serial.log').read_text(errors='replace'):
            if time.monotonic()>deadline:raise RuntimeError('RA analog VM timeout: '+str(Q))
            time.sleep(.2)
    finally:
        if vm.poll() is None:vm.terminate()
        try:vm.wait(timeout=10)
        except subprocess.TimeoutExpired:
            vm.kill();vm.wait(timeout=10)
assert 'reboot: System halted' in (Q/'serial.log').read_text(errors='replace'),str(Q)
results={}
for mode in ('analog','digital'):
    log=(Q/(mode+'.log')).read_text(errors='replace')
    assert 'QA_ANALOG_CORE mode='+mode+' passed=1 frames=120' in log,log[-3000:]
    assert 'Found joypad driver: "lilsmoke"' in log,log[-3000:]
    assert (Q/(mode+'.exit')).read_text().strip()=='0',log[-3000:]
    assert (Q/(mode+'.cfg')).read_text().startswith('audio_enable = false\n'),str(Q)
    assert 'Failed to initialize audio driver' not in log,log[-3000:]
    assert 'Started synchronous audio driver' not in log,log[-3000:]
    assert '[Environ]: SHUTDOWN.' in log,log[-3000:]
    assert '[Core]: Unloading core..' in log,log[-3000:]
    assert 'No content, starting dummy core.' not in log,log[-3000:]
    assert 'QA_PRESENT_DONE game_frames=120 menu_frames=0' in log,log[-3000:]
    if mode=='analog':
        for port in range(4):
            prefix='QA_ANALOG port='+str(port)+' axes='
            axes=[line[len(prefix):] for line in log.splitlines() if line.startswith(prefix)]
            assert len(axes)==1,(port,axes)
            x,y,rx,ry=map(int,axes[0].split(','))
            assert x>2000 and y<-2000 and rx<-2000 and ry>2000,(port,axes)
    results[mode]=dict(passed=True,frames=120,ports=4,driver='lilsmoke',
        audio_disabled=True,orderly_exit=True,exit_code=0)
(Q/'report.json').write_text(json.dumps(dict(passed=True,actual_mips_retroarch_frontend=True,
    native_joypad_driver=True,ports=4,both_sticks=True,digital_core_left_stick=True,
    guest_ram_mib=120,cpus=1,gpu=False,synthetic_controller=True,
    modes=results,orderly_exit=True,vm_timeout_seconds=240,
    native_hc16xx_scanout_tested=False,hardware_fps_measured=False,
    full_os_image_boot_tested=False,physical_console_tested=False),indent=2)+'\n')
print('PASS actual MIPS RetroArch lilsmoke callbacks: both sticks/four ports, digital fallback, orderly exits:',Q)
