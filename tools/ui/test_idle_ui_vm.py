#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Complete production UI navigation in a constrained Linux/DirectFB guest.

The Malta SDL fallback does not certify HC16xx native retention or scanout timing.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

P = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    parser.add_argument('--ram-mib', type=int, choices=(48, 120), default=120)
    parser.add_argument('--recents-session', type=Path, help='Also qualify Recents with this production session helper')
    parser.add_argument('--check-ready-marker', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(exist_ok=False, parents=True)
    sys.path.insert(0, str(P / 'tools/hwtest'))
    sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs, runtime_libraries
    from vm_scene_smoke import Qmp
    from PIL import Image
    overlay = Newc()
    overlay.file('usr/bin/lilsmoke-ui', args.binary.read_bytes(), 0o755)
    fixture_setup = ''
    ui_command = '/usr/bin/lilsmoke-ui >/bench/ui.log 2>&1\necho UI_VM_EXIT=$?\n'
    if args.recents_session:
        overlay.file('usr/lib/lilsmoke/lilsmoke-session', args.recents_session.read_bytes(), 0o755)
        overlay.file('usr/lib/lilsmoke/lilsmoke-launch', (P / 'package/lilsmoke-scripts/src/lilsmoke-launch').read_bytes(), 0o755)
        overlay.file('usr/lib/libretro/fceumm_libretro.so', 'fixture provider availability, not an emulator\n')
        overlay.file('usr/bin/fixture-router', '''#!/bin/sh
case "$1" in
resolve) echo ra:fceumm ;;
run) case "$3" in *fail.nes) exit 7 ;; esac; sleep 1; exit 0 ;;
*) exit 2 ;;
esac
''', 0o755)
        overlay.file('usr/bin/fixture-resolver', '#!/bin/sh\nprintf "%s\\n" "$2"\n', 0o755)
        fixture_setup = '''mount -t tmpfs -o size=2m tmpfs /storage || poweroff -f
mkdir -p /storage/roms/nes /storage/.lilsmoke
echo fixture >/storage/roms/nes/game.nes
echo fixture >/storage/roms/nes/fail.nes
cat >/storage/.lilsmoke/recent-games.v1 <<'HISTORY'
LILSMOKE_RECENTS_1 1
60000 60000 1 "nes" "/storage/roms/nes/game.nes" "Session fixture"
2000 2000 1 "nes" "/storage/roms/nes/fail.nes" "Failed-launch fixture"
1000 1000 1 "nes" "/storage/roms/nes/missing.nes" "Missing fixture"
HISTORY
cp /storage/.lilsmoke/recent-games.v1 /bench/history-before.txt
export LILSMOKE_EMULATOR_ROUTER=/usr/bin/fixture-router LILSMOKE_ROM_RESOLVER=/usr/bin/fixture-resolver
'''
        ui_command = '''for phase in failure success final; do
rm -f /run/lilsmoke-ui-ready
/usr/bin/lilsmoke-ui >"/bench/ui-$phase.log" 2>&1 &
ui_pid=$!
(
    while [ ! -f /run/lilsmoke-ui-ready ] && kill -0 "$ui_pid" 2>/dev/null; do sleep 0.05; done
    [ ! -f /run/lilsmoke-ui-ready ] || cp /run/lilsmoke-ui-ready "/bench/ready-$phase.txt"
) &
ready_pid=$!
wait "$ui_pid"
ui_status=$?
wait "$ready_pid"
echo UI_VM_PHASE_$phase=$ui_status
cp /storage/.lilsmoke/recent-games.v1 "/bench/history-$phase.txt"
done
echo UI_VM_EXIT=0
'''
    overlay.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
    overlay.file('etc/lilsmoke/input-maps/default.map',
                 (P / 'board/qemu-mipsel/input-maps/default.map').read_bytes())
    overlay.file('persist/config/lilsmoke.conf',
                 'DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\nSCAN_ROMS_ON_BOOT=off\n'
                 'DEFAULT_THEME=default\nPOINTER_ENABLED=off\nUI_RENDER_SCALE=native\n')
    overlay.file('usr/bin/bench-init', '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /persist/config /persist/logs /dev/pts /dev/shm /storage/roms /storage/independent-apps
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t tmpfs -o size=1m tmpfs /run || poweroff -f
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_AUDIO_OVERRIDE=dummy LILSMOKE_UI_PROFILE=1 LILSMOKE_UI_IDLE_REUSE=1
export LILSMOKE_BOOT_READY_FILE=/run/lilsmoke-ui-ready
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
cat /proc/meminfo > /bench/memory.txt
''' + fixture_setup + ui_command + '''
[ ! -f /run/lilsmoke-ui-ready ] || cp /run/lilsmoke-ui-ready /bench/first-submitted-frame.txt
sync
poweroff -f
''', 0o755)
    runtime_libraries(args.sdk, overlay, [args.binary] + ([args.recents_session] if args.recents_session else []))
    root = args.out / 'sdk-snapshot.squashfs'
    subprocess.run([str(args.sdk / 'host/bin/mksquashfs'), str(args.sdk / 'target'), str(root),
                    '-noappend', '-comp', 'gzip', '-processors', '1', '-no-xattrs'],
                   check=True, stdout=subprocess.DEVNULL)
    initrd = args.out / 'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(args.sdk, overlay, '/usr/bin/bench-init',
        overlay_mib=16, root_device='/dev/sda', root_fstype='squashfs')))
    command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel', '-M', 'malta',
               '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', str(args.ram_mib), '-kernel', str(args.sdk / 'images/vmlinux'),
               '-initrd', str(initrd), '-append', 'console=ttyS0 video=cirrusfb:640x480M-24@60 panic=-1',
               '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
               '-device', 'usb-kbd', '-qmp', f'unix:{args.out}/qmp.sock,server=on,wait=off',
               '-drive', f'file={root},if=ide,index=0,media=disk,format=raw,snapshot=on',
               '-fsdev', f'local,id=bench,path={args.out},security_model=none',
               '-device', 'virtio-9p-pci,fsdev=bench,mount_tag=bench']
    (args.out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')

    def read(name):
        path = args.out / name
        return path.read_text(errors='replace') if path.exists() else ''

    vm = None
    qmp = None
    events = []
    try:
        with (args.out / 'serial.log').open('wb') as serial:
            vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=serial, stderr=subprocess.STDOUT)

            def wait(predicate, name, seconds=120):
                deadline = time.monotonic() + seconds
                while not predicate():
                    if vm.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError(name + '\n' + read('serial.log')[-2000:] + '\n' + read('ui.log')[-2000:])
                    time.sleep(.3)

            initial_log = 'ui-failure.log' if args.recents_session else 'ui.log'
            first_view = 'RecentGames: loaded 3 entries' if args.recents_session else 'SystemView: first frame complete'
            wait(lambda: first_view in read(initial_log), 'Initial view did not render')
            if args.recents_session:
                wait(lambda: read('ready-failure.txt') == 'UI_FIRST_SUBMITTED_FRAME\n', 'Initial first frame not submitted')
            qmp = Qmp(args.out / 'qmp.sock')

            def key(code):
                qmp.call('send-key', dict(keys=[dict(type='qcode', data=code)], **{'hold-time': 150}))
                events.append(code)
                time.sleep(1)

            def capture(name):
                qmp.call('screendump', dict(filename=str(args.out / (name + '.ppm'))))
                with Image.open(args.out / (name + '.ppm')) as image:
                    assert len(image.getcolors(image.width * image.height) or []) > 8, 'Blank UI capture'
                    image.save(args.out / (name + '.png'))

            time.sleep(2)
            if args.recents_session:
                capture('recents-start')
                key('down')
                key('ctrl')
                wait(lambda: 'UI_VM_PHASE_failure=7' in read('serial.log'), 'Failed launch status was not preserved')
                wait(lambda: first_view in read('ui-success.log'), 'Recents did not resume after failed launch')
                wait(lambda: read('ready-success.txt') == 'UI_FIRST_SUBMITTED_FRAME\n', 'Post-failure first frame not submitted')
                assert read('history-before.txt') == read('history-failure.txt'), 'Failed launch changed history'
                key('down')
                key('down')
                key('ctrl')
                capture('recents-missing')
                assert 'UI_VM_PHASE_success=' not in read('serial.log'), 'Missing game was launched'
                key('up')
                key('up')
                key('ctrl')
                wait(lambda: 'UI_VM_PHASE_success=0' in read('serial.log'), 'Successful session did not return')
                wait(lambda: first_view in read('ui-final.log'), 'Recents did not resume after success')
                wait(lambda: read('ready-final.txt') == 'UI_FIRST_SUBMITTED_FRAME\n', 'Post-session first frame not submitted')
                capture('recents-return')
                lines = read('history-success.txt').splitlines()
                fields = shlex.split(lines[1])
                assert fields[2] == '2' and int(fields[1]) >= 900 and int(fields[0]) == 60000 + int(fields[1]), fields
                key('alt')
                capture('home')
                key('right')
                key('ctrl')
                wait(lambda: 'Applications: catalog ready' in read('ui-final.log'), 'Applications did not open')
                capture('applications')
                key('alt')
                capture('home-return')
                key('right')
                key('ctrl')
                capture('recents-reopened')
                assert read('ui-final.log').count(first_view) >= 2, 'Recents navigation from Home failed'
                names = ('recents-start', 'recents-missing', 'recents-return', 'home', 'applications', 'home-return', 'recents-reopened')
            else:
                capture('home')
                key('ctrl')
                wait(lambda: 'Applications: catalog ready' in read('ui.log'), 'Applications did not open')
                capture('applications')
                key('alt')
                capture('home-return')
                key('right')
                capture('selection-right')
                key('left')
                capture('selection-return')
                names = ('home', 'applications', 'home-return', 'selection-right', 'selection-return')
            key('power')
            wait(lambda: 'UI_VM_EXIT=0' in read('serial.log'), 'UI did not cleanly exit')
            wait(lambda: 'reboot: System halted' in read('serial.log'), 'Guest did not halt')
        logs = read('ui.log') + read('ui-failure.log') + read('ui-success.log') + read('ui-final.log')
        assert 'LilSmokeUI exiting' in logs
        assert not re.search(r'Out of memory|oom-kill|Killed process', read('serial.log') + logs)
        marker = read('first-submitted-frame.txt') == 'UI_FIRST_SUBMITTED_FRAME\n'
        if args.check_ready_marker or args.recents_session:
            assert marker, 'First-submitted-frame RAM marker missing'
        assert re.search(r'SwapTotal:\s+0\s+kB', read('memory.txt'))
        images = {name: hashlib.sha256((args.out / (name + '.png')).read_bytes()).hexdigest()
                  for name in names}
        assert images['home'] != images['applications'], images
        if not args.recents_session:
            assert images['home'] == images['home-return'], images
            assert images['home'] != images['selection-right'] and images['home'] == images['selection-return'], images
        report = dict(passed=True, actual_ui_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                      home_applications_home=True, post_return_selection=True, clean_ui_exit=True,
                      events=events, screenshots=images, cpus=1, ram_mib=args.ram_mib, swap_kib=0,
                      hardware_3d=False, physical_hc16xx=False, physical_smoothness_qualified=False,
                      native_retention_qualified=False, native_retention_boundary='Separate paired production-view tests',
                      sd_access=False, sdk_installed=False)
        report['first_submitted_frame_marker'] = marker
        report['recents_sessions_qualified'] = bool(args.recents_session)
        report['engine_boundary'] = 'Production Launcher, launch script and session supervisor; deterministic fixture engine, not gameplay'
        if args.recents_session:
            report['session_sha256'] = hashlib.sha256(args.recents_session.read_bytes()).hexdigest()
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
    except Exception as error:
        (args.out / 'report.json').write_text(json.dumps(dict(passed=False, error=str(error), events=events,
                                                           ram_mib=args.ram_mib), indent=2) + '\n')
        raise
    finally:
        if qmp:
            qmp.close()
        if vm is not None and vm.poll() is None:
            vm.terminate()
            try:
                vm.wait(timeout=10)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait()
        root.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
