"""Real MIPS SDL virtual driver -> shared state -> native SM64 input bindings."""
import gzip
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
parser=argparse.ArgumentParser()
parser.add_argument('--rk707',action='store_true')
parser.add_argument('--portable-usb',action='store_true')
parser.add_argument('--live',choices=('ps','xinput'))
parser.add_argument('--driver',type=Path,default=B/'target/usr/bin/lilsmoke-usb-gamepads')
parser.add_argument('--deny-file-locks',action='store_true')
args=parser.parse_args()
Q=Path(tempfile.mkdtemp(prefix='usb-pad-',dir='/root'))
sys.path.insert(0,str(P/'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs,disk_devices,runtime_libraries
S=P/'package/lilsmoke-ui/src/src'
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-g++'),'-std=c++17','-O2','-Wall','-Wextra','-Werror',
    '-include',str(P/'package/lilsmoke-od-sdl/src/input-wire.h'),'-I'+str(S),'-I'+str(B/'staging/usr/include/SDL2'),
    str(P/'tools/gamepads/usb_probe.cpp'),*[str(S/(name+'.cpp')) for name in ['Input','Keymap','Log']],
    '-lSDL2','-o',str(Q/'probe')],check=True)
init='''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /run /persist /dev/pts
mount -t devpts devpts /dev/pts
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
BOOT_SERVICE_REGRESSION
/usr/bin/lilsmoke-usb-gamepads --fixture > /bench/driver.log 2>&1 &
driver=$!
/bench/probe > /bench/probe.log 2>&1
echo QA_USB_PROBE_EXIT=$?
wait "$driver"
cat /bench/driver.log /bench/probe.log
sync
poweroff -f
'''
if args.rk707:init=init.replace('--fixture','--rk707-fixture')
if args.live:
    assert args.portable_usb
    init=init.replace('--fixture','').replace('/bench/probe >','/bench/probe --live >')
    init=init.replace('wait "$driver"','kill -TERM "$driver"; wait "$driver"')
    init=init.replace('/usr/bin/lilsmoke-usb-gamepads  >','LILSMOKE_GAMEPADS_SHARED_ONLY=1 /usr/bin/lilsmoke-usb-gamepads >')
if args.portable_usb:
    subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-shared','-fPIC','-O2',
        '-I'+str(B/'staging/usr/include/SDL2'),
        str(P/'tools/gamepads/deny_optional_usb.c'),'-ldl','-o',str(Q/'deny.so')],check=True)
    init=init.replace('/usr/bin/lilsmoke-usb-gamepads', '''LD_PRELOAD=/bench/deny.so /usr/bin/lilsmoke-usb-probe > /bench/usb-original.log 2>&1
echo QA_ORIGINAL_USB_EXIT=$?
LD_PRELOAD=/bench/deny.so LD_LIBRARY_PATH=/usr/lib/lilsmoke-usb:/usr/lib:/lib /usr/bin/lilsmoke-usb-probe > /bench/usb-portable.log 2>&1
echo QA_PORTABLE_USB_EXIT=$?
export LD_PRELOAD=/bench/deny.so LD_LIBRARY_PATH=/usr/lib/lilsmoke-usb:/usr/lib:/lib
/usr/bin/lilsmoke-usb-gamepads''',1)
    if args.live:
        init=init.replace('export LD_PRELOAD=/bench/deny.so', 'export LILSMOKE_GAMEPADS_SHARED_ONLY=1 LILSMOKE_USB_QA_TRACE=1\nexport LD_PRELOAD=/bench/deny.so')
boot=(P/'board/segam-m8-v6-stock/m8-launch.sh').read_text()
dispatch=boot[boot.index('# File-backed /os links'):boot.index('# The vendor kernel uses auddec')]
regression='''ROOT=/tmp/service-root
mkdir -p "$ROOT/os/files" "$ROOT/bin" "$ROOT/usr/bin" "$ROOT/lib" "$ROOT/etc/init.d" "$ROOT/run" "$ROOT/persist/logs"
cp /bin/busybox "$ROOT/os/files/qa-busybox"
cp /lib/libc.so "$ROOT/lib/libc.so"
cp /lib/ld-musl-mipsel.so.1 "$ROOT/lib/ld-musl-mipsel.so.1"
ln -s /os/files/qa-busybox "$ROOT/bin/busybox"
ln -s busybox "$ROOT/bin/sh"
for name in mkdir start-stop-daemon sleep; do ln -s ../../bin/busybox "$ROOT/usr/bin/$name"; done
mkdir -p "$ROOT/usr/lib/lilsmoke" "$ROOT/storage" "$ROOT/dev"
mknod "$ROOT/dev/null" c 1 3
cp /bench/qa-log-helper "$ROOT/usr/lib/lilsmoke/lilsmoke-session-log"
cp /bench/qa-diagnostics "$ROOT/os/files/qa-diagnostics"
cp /bench/qa-driver "$ROOT/os/files/qa-driver"
cp /bench/qa-apps "$ROOT/os/files/qa-apps"
cp /bench/qa-S07 "$ROOT/os/files/qa-S07"
ln -s /os/files/qa-diagnostics "$ROOT/usr/bin/lilsmoke-input-diagnostics"
ln -s /os/files/qa-driver "$ROOT/usr/bin/lilsmoke-usb-gamepads"
ln -s /os/files/qa-S07 "$ROOT/etc/init.d/S07usb-gamepads"
ln -s /os/files/qa-apps "$ROOT/etc/init.d/S08applications"
if [ -x "$ROOT/etc/init.d/S07usb-gamepads" ]; then echo QA_BAD_FIXTURE; poweroff -f; fi
'''+dispatch+'''
attempt=0
while ! grep -q QA_DRIVER_STARTED "$ROOT/persist/logs/usb-gamepads.log" 2>/dev/null && [ "$attempt" -lt 100 ]; do
    sleep .1
    attempt=$((attempt+1))
done
if grep -q QA_DRIVER_STARTED "$ROOT/persist/logs/usb-gamepads.log" &&
   [ -f "$ROOT/persist/logs/diagnostics.log" ] && [ -f "$ROOT/persist/logs/apps.log" ]; then
    echo QA_BOOT_FILE_BACKED=PASS
else echo QA_BOOT_FILE_BACKED=FAIL; fi
kill "$(cat "$ROOT/run/lilsmoke-usb-gamepads.pid")" 2>/dev/null || true
sleep 1
# A corrupt log allocation must not prevent controller-driver startup.
rm -f "$ROOT/run/lilsmoke-usb-gamepads.pid" "$ROOT/persist/logs/usb-gamepads.log"
mkdir "$ROOT/persist/logs/usb-gamepads.log"
chroot "$ROOT" /etc/init.d/S07usb-gamepads start
echo QA_FALLBACK_SERVICE_EXIT=$?
attempt=0
while ! grep -q QA_DRIVER_STARTED "$ROOT/storage/.lilsmoke-diagnostics/logs/usb-gamepads.log" 2>/dev/null && [ "$attempt" -lt 100 ]; do
    sleep .1
    attempt=$((attempt+1))
done
if grep -q QA_DRIVER_STARTED "$ROOT/storage/.lilsmoke-diagnostics/logs/usb-gamepads.log"; then
    echo QA_BOOT_LOG_FALLBACK=PASS
else
    echo QA_BOOT_LOG_FALLBACK=FAIL
    ls -lR "$ROOT/persist" "$ROOT/storage" "$ROOT/run" "$ROOT/usr/lib"
    ps w
fi
kill "$(cat "$ROOT/run/lilsmoke-usb-gamepads.pid")" 2>/dev/null || true
'''
init=init.replace('BOOT_SERVICE_REGRESSION',regression)
if args.deny_file_locks:
    if args.live:
        parser.error('--deny-file-locks is for synthetic VM regressions only')
    subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-shared','-fPIC','-O2',
        str(P/'tools/gamepads/deny_advisory_locks.c'),'-ldl','-o',str(Q/'deny-locks.so')],check=True)
    init=init.replace('/usr/bin/lilsmoke-usb-gamepads ',
        'LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}/bench/deny-locks.so" /usr/bin/lilsmoke-usb-gamepads ',1)
if args.live:
    # Keep the interactive hot loop off 9p; a host filesystem stall must not
    # masquerade as a controller that stopped sending reports.
    init=init.replace('/bench/deny.so','/run/deny.so')
    init=init.replace('/bench/driver.log','/run/driver.log')
    init=init.replace('/bench/probe --live > /bench/probe.log 2>&1',
                      'mkdir -p /run/no-native-input\necho QA_LIVE_READY\nLILSMOKE_INPUT_DIR=/run/no-native-input /run/probe --live')
    init=init.replace('cat /run/driver.log /bench/probe.log',
                      'cat /run/driver.log\ncp /run/driver.log /bench/driver.log')
    mount_line='mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f'
    init=init.replace(mount_line,mount_line+'\ncp /bench/probe /run/probe\ncp /bench/deny.so /run/deny.so')
for name,body in {
    'qa-diagnostics':'#!/bin/sh\necho diagnostics > /persist/logs/diagnostics.log\n',
    'qa-driver':'#!/bin/sh\necho QA_DRIVER_STARTED >&2\nsleep 3\n',
    'qa-apps':'#!/bin/sh\necho apps > /persist/logs/apps.log\n',
    'qa-S07':(P/'package/lilsmoke-usb-gamepads/S07usb-gamepads').read_text(),
    'qa-log-helper':(P/'package/lilsmoke-scripts/src/lilsmoke-session-log').read_text(),
}.items():
    path=Q/name;path.write_text(body);path.chmod(0o755)
overlay=Newc();overlay.file('usr/bin/bench-init',init,0o755)
binary=args.driver
overlay.file('usr/bin/lilsmoke-usb-gamepads',binary.read_bytes(),0o755)
runtime_libraries(B,overlay,[binary,Q/'probe'])
if args.portable_usb:
    overlay.file('usr/lib/lilsmoke-usb/libusb-1.0.so.0',(B/'target/usr/lib/lilsmoke-usb/libusb-1.0.so.0').read_bytes(),0o755)
    overlay.file('usr/bin/lilsmoke-usb-probe',(B/'target/usr/bin/lilsmoke-usb-probe').read_bytes(),0o755)
overlay.file('usr/lib/libusb-1.0.so.0',(B/'target/usr/lib/libusb-1.0.so.0').read_bytes(),0o755)
initrd=Q/'boot.cpio.gz';initrd.write_bytes(gzip.compress(overlay_initramfs(B,overlay,'/usr/bin/bench-init',overlay_mib=32,rootfs_in_ram=True)))
cmd=['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel','-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','120',
    '-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),'-append','console=ttyS0 panic=-1','-display','none',
    '-device','usb-kbd',
    '-serial','stdio','-monitor','none','-no-reboot',*disk_devices(B,[('bench',Q,False)])]
if args.live:
    pair='vendorid=0x0079,productid=0x181c' if args.live=='ps' else 'vendorid=0x045e,productid=0x028e'
    cmd += ['-device','usb-host,'+pair+',id=rk707']
with (Q/'serial.log').open('wb') as log:
    vm=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    try:
        deadline=time.monotonic()+(720 if args.live else 90)
        while vm.poll() is None and 'reboot: System halted' not in (Q/'serial.log').read_text(errors='replace'):
            if time.monotonic()>deadline:raise RuntimeError('USB input VM timeout: '+str(Q))
            time.sleep(.2)
    finally:
        if vm.poll() is None:vm.terminate()
        vm.wait(timeout=10)
text=(Q/'serial.log').read_text(errors='replace')
assert 'QA_USB_PROBE_EXIT=0' in text and 'USB_PAD_PROBE seen=1 released=1' in text,text[-3000:]
assert 'QA_BOOT_FILE_BACKED=PASS' in text,text[-3000:]
assert 'QA_BOOT_LOG_FALLBACK=PASS' in text,text[-3000:]
if args.live:
    assert 'SM64_CONTROLLER native_devices=0' in text,text[-3000:]
    driver=(Q/'driver.log').read_text()
    if args.live=='xinput':
        assert 'hidapi=1' in driver and 'backend=shared-state' in driver,driver
        assert 'vendor=045e product=028e' in driver,driver
if args.portable_usb:
    assert 'QA_PORTABLE_USB_EXIT=0' in text and 'QA_ORIGINAL_USB_EXIT=1' in text,text[-3000:]
    usb=(Q/'usb-portable.log').read_text()
    assert 'init=0' in usb and 'vendor=0627 product=0001' in usb,usb
if args.deny_file_locks:
    assert 'LOCK_QA flock=ENOSYS fcntl_setlk=ENOSYS errno=89' in text,text[-4000:]
    assert 'USB_INPUT state_owner=mkdir' in text,text[-4000:]
(Q/'report.json').write_text(json.dumps(dict(passed=True,guest_ram_mib=120,cpus=1,gpu=False,
    real_sdl_driver=True,synthetic_controller_fixture=not bool(args.live),native_bindings=True,
    button_release_tested=True,stale_disconnect_releases=not bool(args.live),
    production_boot_dispatch_tested=True,absolute_symlink_regression=True,background_service_log_tested=True,
    broken_persistent_log_fallback=True,
    rk707_packet_fixture=args.rk707,netlink_eventfd_timerfd_denial_tested=args.portable_usb,
    physical_dongles_tested=bool(args.live),live_mode=args.live,physical_console_tested=False,
    actual_mips_linux_kernel=True,advisory_lock_calls_disabled=args.deny_file_locks,
    daemon_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()),indent=2)+'\n')
print('PASS MIPS driver -> IPC -> native X attack/LT crouch/right-stick camera; input releases.',
      'Physical receiver' if args.live else 'Synthetic fixture',Q)
