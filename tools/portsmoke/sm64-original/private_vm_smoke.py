"""Private full-original-engine run in CPU-only MIPS Linux, not a game recreation."""
import argparse
import errno
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from PIL import Image

HERE=Path(__file__).resolve().parent
P=HERE.parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
def read_progress(path):
    # DrvFS can transiently return ENODATA while QEMU/9p writes an evidence log.
    for attempt in range(4):
        try:
            return path.read_text(errors='replace')
        except OSError as error:
            if error.errno not in (errno.ENODATA,errno.EAGAIN,errno.EINTR) or attempt == 3:
                raise
            time.sleep(0.05)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',type=Path,default=Path('/root/lilsmoke-sm64-original-vm-20261011'))
parser.add_argument('--render-stride',type=int,default=1,choices=range(1,16))
parser.add_argument('--width',type=int,choices=(160,192,256,320),help='explicit fixed width; omit to test the application default')
parser.add_argument('--quality',choices=('performance','balanced','quality','adaptive','detail'))
parser.add_argument('--scale',choices=('nearest','smooth'))
parser.add_argument('--input-probe',action='store_true',help='use private QMP input after the original castle intro')
parser.add_argument('--menu-probe',action='store_true',help='exercise Start+X options, saved settings and resume via guest input')
parser.add_argument('--native-audio',action='store_true',help='exercise the M8 queue with an unavailable device, never fake audible output')
parser.add_argument('--package',type=Path,help='install/run the private local .lilpck through the actual Apps helper')
parser.add_argument('--storage-template',type=Path,help='private previous-run storage for settings/save restart checks')
args=parser.parse_args()
if args.menu_probe: args.input_probe=True
Q=args.out
Q.mkdir(exist_ok=False)
sys.path.insert(0,str(P/'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
if args.package:
    import shutil
    if args.storage_template: shutil.copytree(args.storage_template,Q/'storage')
    else: (Q/'storage').mkdir()
    shutil.copyfile(args.package,Q/'SM64-local.lilpck')
sys.path.insert(0,str(P/'tools/portsmoke/sm64'))
from vm_scene_smoke import Qmp
build=json.loads((HERE/'_build/mips-build.json').read_text())
assert not build['distributable']
binary=Path(build['privateBinary'])
assert hashlib.sha256(binary.read_bytes()).hexdigest()==build['binarySha256']
init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /engine /persist /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,ro engine /engine || poweroff -f
mount -t tmpfs -o size=8m tmpfs /dev/shm
export HOME=/persist SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export SM64_ORIGINAL_TEST_FRAMES=3000 SM64_ORIGINAL_RENDER_EVERY=RENDER_STRIDE SM64_ORIGINAL_EVIDENCE=/bench
(
    while :; do
        sleep 5
        for status in /proc/[0-9]*/comm; do
            read -r name < "$status" || continue
            [ "$name" = sm64-original ] || [ "$name" = sm64.us.f3dex2e ] || continue
            pid=${status%/comm}
            printf '\nProcess=%s\n' "$pid"
            cat "$pid/status" "$pid/wchan" "$pid/stack" 2>/dev/null
        done
    done
) > /bench/process.log 2>&1 &
cat /proc/meminfo > /bench/memory.txt
cd /persist
ulimit -v 65536
/engine/sm64.us.f3dex2e > /bench/engine.log 2>&1
echo QA_ORIGINAL_ENGINE_EXIT=$?
cat /bench/engine.log
sync
poweroff -f
'''
init=init.replace('RENDER_STRIDE',str(args.render_stride))
if args.width is not None:
    init=init.replace('cat /proc/meminfo', f'export SM64_SOFTWARE_WIDTH={args.width}\ncat /proc/meminfo')
if args.quality:
    init=init.replace('cat /proc/meminfo', f'export SM64_WORLD_QUALITY={args.quality}\ncat /proc/meminfo')
if args.scale:
    init=init.replace('cat /proc/meminfo', f'export SM64_WORLD_SCALE={args.scale}\ncat /proc/meminfo')
if args.input_probe:
    init=init.replace('SM64_ORIGINAL_TEST_FRAMES=3000','SM64_ORIGINAL_TEST_FRAMES=3600')
    init=init.replace('cat /proc/meminfo', 'export SM64_ORIGINAL_INTERACTIVE_AFTER=3000 SM64_ORIGINAL_INPUT_TRACE=1\ncat /proc/meminfo')
if args.native_audio:
    init=init.replace('cat /proc/meminfo', 'export LILSMOKE_M8_AUDIO=1\ncat /proc/meminfo')
if args.package:
    init=init.replace('cat /proc/meminfo', """mkdir -p /storage /persist/logs
mount -t 9p -o trans=virtio,version=9p2000.L storage /storage || poweroff -f
export LILSMOKE_PRIVATE_HARDWARE_QA=1 LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
ln -s /bench/engine.log /persist/logs/sm64-original-local.log
if [ ! -f '/storage/independent-apps/Super Mario 64 Local Test/meta.json' ]; then
    /usr/lib/lilsmoke/lilsmoke-apps install /bench/SM64-local.lilpck || poweroff -f
fi
cat /proc/meminfo""")
    init=init.replace('/engine/sm64.us.f3dex2e > /bench/engine.log 2>&1',
        "/usr/lib/lilsmoke/lilsmoke-apps run 'Super Mario 64 Local Test'")
overlay=Newc();overlay.file('usr/bin/bench-init',init,0o755)
runtime_libraries(B,overlay,[binary])
if args.package:
    helper=B/'target/usr/lib/lilsmoke/lilsmoke-apps'
    overlay.file('usr/lib/lilsmoke/lilsmoke-apps',helper.read_bytes(),0o755)
    runtime_libraries(B,overlay,[helper])
overlay.file('etc/directfbrc',(P/'board/qemu-mipsel/directfbrc').read_bytes())
initrd=Q/'boot.cpio.gz';initrd.write_bytes(gzip.compress(overlay_initramfs(B,overlay,'/usr/bin/bench-init',overlay_mib=16,rootfs_in_ram=True)))
qmp_directory=tempfile.TemporaryDirectory(prefix='sm64-qmp-')
qmp_socket=Path(qmp_directory.name)/'qmp.sock'
cmd=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta','-cpu','74Kf-lilsmoke','-smp','1',
     '-m','120','-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),'-append','console=ttyS0 video=cirrusfb:640x480M-16@60 panic=-1',
     '-vga','cirrus','-display','none','-serial','stdio','-monitor','none','-no-reboot','-device','usb-kbd',
     '-qmp',f'unix:{qmp_socket},server=on,wait=off',
     *disk_devices(B,[('bench',Q,False),('engine',binary.parent,True)]+([('storage',Q/'storage',False)] if args.package else []))]
with (Q/'serial.log').open('wb') as log:
    vm=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    qmp=None;sent=False;menu_sent=False;menu_tested=False
    try:
        deadline=time.monotonic()+900
        while 'reboot: System halted' not in read_progress(Q/'serial.log'):
            if vm.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Full original MIPS engine failed; inspect private serial.log')
            if args.input_probe and not sent and (Q/'engine.log').exists() and 'OriginalFrame=3000 ' in read_progress(Q/'engine.log'):
                qmp=Qmp(qmp_socket)
                qmp.call('send-key',dict(keys=[dict(type='qcode',data='up'),dict(type='qcode',data='ctrl')],**{'hold-time':1500}))
                sent=True
            if args.menu_probe and sent and not menu_sent and 'OriginalFrame=3030 ' in read_progress(Q/'engine.log'):
                qmp.call('send-key',dict(keys=[dict(type='qcode',data='ret'),dict(type='qcode',data='spc')],**{'hold-time':300}))
                menu_sent=True
            if args.menu_probe and menu_sent and not menu_tested and 'SM64_MENU opened' in read_progress(Q/'engine.log'):
                time.sleep(1)
                qmp.call('screendump',{'filename':str(Q/'menu-screen.ppm')})
                for key in ('down','right','down','right','down','left','alt'):
                    qmp.call('send-key',dict(keys=[dict(type='qcode',data=key)],**{'hold-time':120}))
                    time.sleep(0.35)
                menu_tested=True
            time.sleep(1)
    finally:
        if qmp:qmp.close()
        if vm.poll() is None:vm.terminate()
        try:vm.wait(timeout=10)
        except subprocess.TimeoutExpired:vm.kill();vm.wait()
        qmp_directory.cleanup()
serial=(Q/'serial.log').read_text(errors='replace')
text=(Q/'engine.log').read_text(errors='replace')
assert 'QA_ORIGINAL_ENGINE_EXIT=0' in serial and 'OriginalEngineTest=FINISHED' in text,serial[-2500:]
expected_frames=3600 if args.input_probe else 3000
if args.render_stride == 1:
    assert f'Frames={expected_frames} Rendered={expected_frames}' in text
assert 'hud=320x240' in text
assert re.search(r'OriginalFrame=\d+ Level=16 Area=1',text) and re.search(r'AudioNonzero=[1-9]\d*',text)
if args.input_probe:
    assert sent and 'SM64_CONTROLLER native_devices=1' in text
    assert 'SM64_CONTROLLER Buttons=8000 Stick=0,80' in text,text[-2000:]
    positions=re.findall(r'OriginalFrame=(\d+).*Position=([^\n]+)',text)
    assert len({position for frame,position in positions if int(frame)>=3000})>1,'Mario did not move under native input'
if args.native_audio:
    assert 'SM64_AUDIO native=1 input_rate=32000 output_rate=44100 nonblocking=1' in text
    assert text.count('POCKETSNES_AUDIO init-request')==(2 if args.menu_probe else 1),'Unexpected worker restart loop'
if args.menu_probe:
    assert menu_tested and 'SM64_MENU resumed' in text
    opened=re.search(r'SM64_MENU opened frame=(\d+) timer=(\d+)',text).groups()
    resumed=re.search(r'SM64_MENU resumed frame=(\d+) timer=(\d+)',text).groups()
    assert opened==resumed,'Game logic advanced in the options menu'
    options=Q/'storage/independent-apps/Super Mario 64 Local Test/saves/.sm64-port/lilsmoke-options.conf'
    assert options.read_text()=='Quality=balanced\nScale=smooth\nVolume=95\n'
    im=Image.open(Q/'menu-screen.ppm');assert len(im.getcolors(im.width*im.height))>4;im.save(Q/'menu-screen.png')
images=[]
for file in sorted(Q.glob('frame-*.ppm')):
    im=Image.open(file);assert im.size==(320,240)
    colors=im.getcolors(320*240);assert colors and len(colors)>4
    im.save(file.with_suffix('.png'));images.append(dict(file=file.name,colors=len(colors)))
assert len(images)>=6
process_text=(Q/'process.log').read_text(errors='replace')
def observed_kib(field):
    return max((int(value) for value in re.findall(r'^'+field+r':\s+(\d+) kB',process_text,re.M)),default=0)
report=dict(passed=True,original_game_engine=True,private_binary_not_for_distribution=True,
            binary_sha256=build['binarySha256'],
            guest_ram_mib=120,process_address_space_mib=64,cpus=1,cpu='74Kf-lilsmoke',
            observed_max_rss_kib=observed_kib('VmRSS'),observed_max_virtual_kib=observed_kib('VmSize'),
            observed_max_vmpeak_kib=observed_kib('VmPeak'),memory_sample_interval_seconds=5,
            hardware_3d=False,graphics='SDL2 DirectFB software / framebuffer',fixed_width_override=args.width,
            world_quality=re.search(r'SM64_VIDEO[^\n]+quality=(\w+)',text).group(1),
            requested_quality=args.quality,world_scale=args.scale or 'nearest',
            initial_raster_width=int(re.search(r'SM64_VIDEO world=(\d+)x',text).group(1)),
            quality_transitions=re.findall(r'SM64_QUALITY[^\n]+',text),
            original_engine_files_unmodified=2500,frames=3600 if args.input_probe else 3000,render_stride=args.render_stride,images=images,
            title_file_select_castle_grounds_reached=True,original_audio_synthesis_nonzero=True,
            native_input_exercised=args.input_probe,native_audio_queue_exercised=args.native_audio,
            menu_pause_settings_resume_exercised=args.menu_probe,
            installed_and_run_as_lilsmoke_application=bool(args.package),
            previous_storage_reused=bool(args.storage_template),
            audible_hardware_audio_tested=False,
            physical_console_fps_measured=False,notice='Malta is not HC16xx. Full rendering is required to advance render-driven cutscene camera behavior faithfully; no physical performance claim.')
(Q/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS original full game title -> file select -> castle grounds, original audio synthesis, native adapters requested:',args.input_probe,args.native_audio,'frames:',report['frames'],'CPU-only MIPS Linux 120 MiB / process 64 MiB. Private test only, not physical FPS or a released package.')
