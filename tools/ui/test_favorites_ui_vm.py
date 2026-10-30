#!/usr/bin/env python3
"""Real UI Favorites/Hide Empty workflow, 48 MiB / one CPU / no swap.

Uses production UI, session supervisor and launcher with a labeled fixture
engine. Does not qualify actual gameplay, hardware scanout or an SD card.
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


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(P / 'tools/hwtest'))
    sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs, runtime_libraries
    from vm_scene_smoke import Qmp
    from PIL import Image
    overlay = Newc()
    overlay.file('usr/bin/lilsmoke-ui', args.binary.read_bytes(), 0o755)
    overlay.file('usr/lib/lilsmoke/lilsmoke-session', args.session.read_bytes(), 0o755)
    overlay.file('usr/lib/lilsmoke/lilsmoke-launch', (P / 'package/lilsmoke-scripts/src/lilsmoke-launch').read_bytes(), 0o755)
    for name in ('fceumm_libretro.so', 'snes9x_2005_libretro.so'):
        overlay.file('usr/lib/libretro/' + name, 'fixture engine availability, not an emulator\n')
    overlay.file('usr/bin/fixture-router', '#!/bin/sh\ncase "$1" in resolve) echo ra:fceumm ;; run) sleep 1; exit 0 ;; *) exit 2 ;; esac\n', 0o755)
    overlay.file('usr/bin/fixture-resolver', '#!/bin/sh\nprintf "%s\\n" "$2"\n', 0o755)
    overlay.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
    overlay.file('etc/lilsmoke/input-maps/default.map', (P / 'board/qemu-mipsel/input-maps/default.map').read_bytes())
    overlay.file('persist/config/lilsmoke.conf', 'DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\nSCAN_ROMS_ON_BOOT=off\nDEFAULT_THEME=default\nPOINTER_ENABLED=off\nUI_RENDER_MODE=native\n')
    overlay.file('usr/bin/bench-init', '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /persist/config /persist/logs /dev/pts /dev/shm /storage/roms /run
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t tmpfs -o size=1m tmpfs /run || poweroff -f
mount -t tmpfs -o size=2m tmpfs /storage || poweroff -f
mkdir -p /storage/roms/nes /storage/roms/snes /storage/.lilsmoke
echo fixture >/storage/roms/nes/game.nes
cat >/storage/.lilsmoke/favorites.v1 <<'FAVORITES'
LILSMOKE_FAVORITES_1 1 1
"nes" "/storage/roms/nes/missing.nes" "Missing fixture"
END
FAVORITES
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_AUDIO_OVERRIDE=dummy LILSMOKE_UI_PROFILE=1 LILSMOKE_UI_IDLE_REUSE=1
export LILSMOKE_BOOT_READY_FILE=/run/lilsmoke-ui-ready
export LILSMOKE_EMULATOR_ROUTER=/usr/bin/fixture-router LILSMOKE_ROM_RESOLVER=/usr/bin/fixture-resolver
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
cat /proc/meminfo >/bench/memory.txt
for phase in edit return; do
    rm -f /run/lilsmoke-ui-ready
    /usr/bin/lilsmoke-ui >"/bench/ui-$phase.log" 2>&1 &
    ui_pid=$!
    (
        while [ ! -f /run/lilsmoke-ui-ready ] && kill -0 "$ui_pid" 2>/dev/null; do sleep 0.05; done
        [ ! -f /run/lilsmoke-ui-ready ] || cp /run/lilsmoke-ui-ready "/bench/ready-$phase.txt"
    ) &
    ready_pid=$!
    wait "$ui_pid"
    result=$?
    wait "$ready_pid"
    echo UI_VM_PHASE_$phase=$result
    cp /storage/.lilsmoke/favorites.v1 "/bench/favorites-$phase.txt"
    cp /storage/.lilsmoke/recent-games.v1 "/bench/recents-$phase.txt"
    cp /persist/config/lilsmoke.conf "/bench/config-$phase.txt"
done
cat /proc/meminfo >/bench/memory-final.txt
sync
poweroff -f
''', 0o755)
    runtime_libraries(args.sdk, overlay, [args.binary, args.session])
    root = args.out / 'sdk-snapshot.squashfs'
    subprocess.run([str(args.sdk / 'host/bin/mksquashfs'), str(args.sdk / 'target'), str(root),
                    '-noappend', '-comp', 'gzip', '-processors', '1', '-no-xattrs'], check=True, stdout=subprocess.DEVNULL)
    initrd = args.out / 'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(args.sdk, overlay, '/usr/bin/bench-init',
        overlay_mib=16, root_device='/dev/sda', root_fstype='squashfs')))
    command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel', '-M', 'malta',
        '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '48', '-kernel', str(args.sdk / 'images/vmlinux'),
        '-initrd', str(initrd), '-append', 'console=ttyS0 video=cirrusfb:640x480M-24@60 panic=-1',
        '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot', '-device', 'usb-kbd',
        '-qmp', f'unix:{args.out}/qmp.sock,server=on,wait=off',
        '-drive', f'file={root},if=ide,index=0,media=disk,format=raw,snapshot=on',
        '-fsdev', f'local,id=bench,path={args.out},security_model=none', '-device', 'virtio-9p-pci,fsdev=bench,mount_tag=bench']
    (args.out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    vm = qmp = None
    events, captures = [], []
    report = dict(passed=False, ram_mib=48, cpus=1, swap_kib=0, ui_sha256=sha(args.binary), session_sha256=sha(args.session),
                  runner_sha256=sha(Path(__file__)), sdk_installed=False, sd_access=False, hardware_scanout_qualified=False,
                  engine_boundary='Production Launcher, standard launch script and session helper; labeled fixture engine, not gameplay')
    def read(name):
        path = args.out / name
        return path.read_text(errors='replace') if path.exists() else ''
    def log():
        return read('ui-edit.log') + read('ui-return.log')
    try:
        with (args.out / 'serial.log').open('wb') as serial:
            vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=serial, stderr=subprocess.STDOUT)
            def wait(predicate, message, timeout=120):
                deadline = time.monotonic() + timeout
                while not predicate():
                    if vm.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError(message + '\n' + read('serial.log')[-2000:] + '\n' + log()[-3000:])
                    time.sleep(.25)
            wait(lambda: read('ready-edit.txt') == 'UI_FIRST_SUBMITTED_FRAME\n', 'Initial UI did not submit')
            qmp = Qmp(args.out / 'qmp.sock')
            def key(code, delay=.7):
                qmp.call('send-key', dict(keys=[dict(type='qcode', data=code)], **{'hold-time': 80}))
                events.append(code)
                time.sleep(delay)
            def move(code, count):
                for _ in range(count):
                    key(code, .25)
            def capture(name):
                time.sleep(.35)
                qmp.call('screendump', dict(filename=str(args.out / (name + '.ppm'))))
                with Image.open(args.out / (name + '.ppm')) as image:
                    assert len(image.getcolors(image.width * image.height) or []) > 8, 'Blank capture: ' + name
                    image.save(args.out / (name + '.png'))
                captures.append(name)
            def options():
                previous = log().count('GameOptions: ready')
                key('f2')
                wait(lambda: log().count('GameOptions: ready') > previous, 'Game options did not open')
            def favorite_change(text):
                previous = log().count('GameOptions: ' + text)
                key('down'); key('ctrl')
                wait(lambda: log().count('GameOptions: ' + text) > previous, 'Favorite mutation did not complete: ' + text)
            def enter_favorites(count):
                previous = log().count(f'Favorites: loaded {count} entries')
                key('ctrl')
                wait(lambda: log().count(f'Favorites: loaded {count} entries') > previous, 'Favorites view did not open')
            capture('home-hidden')
            initial = read('ui-edit.log')
            assert 'Home: entry=snes games=0 hide_empty=1' not in initial
            key('ctrl')
            wait(lambda: 'GameList: ready for nes' in log(), 'Library did not open')
            options(); favorite_change('Added to Favorites'); capture('options-added')
            key('alt'); key('alt'); move('right', 3); enter_favorites(2)
            key('ctrl'); capture('missing-favorite')
            assert 'UI_VM_PHASE_edit=' not in read('serial.log'), 'Missing favorite was launched'
            options(); favorite_change('Removed from Favorites'); key('alt')
            wait(lambda: 'Favorites: loaded 1 entries' in log(), 'Missing Favorite removal did not refresh')
            capture('favorites-one')
            options(); favorite_change('Removed from Favorites'); key('alt')
            wait(lambda: 'Favorites: loaded 0 entries' in log(), 'Last Favorite removal did not refresh')
            capture('favorites-empty')
            key('alt'); move('left', 3); key('ctrl'); options(); favorite_change('Added to Favorites')
            key('alt'); key('alt'); move('right', 3); enter_favorites(1); capture('favorites-before-launch'); key('ctrl')
            wait(lambda: 'UI_VM_PHASE_edit=0' in read('serial.log'), 'Favorite launch did not return successfully')
            wait(lambda: read('ready-return.txt') == 'UI_FIRST_SUBMITTED_FRAME\n', 'Restarted UI did not submit')
            assert shlex.split(read('favorites-edit.txt').splitlines()[0]) == ['LILSMOKE_FAVORITES_1', '1', '1']
            session = shlex.split(read('recents-edit.txt').splitlines()[1])
            assert session[2] == '1' and int(session[1]) >= 900, session
            move('right', 3); enter_favorites(1); capture('favorites-after-restart'); key('alt')
            move('right', 4); key('ctrl'); move('down', 29); key('left')
            wait(lambda: 'Library: hide empty systems=0' in read('ui-return.log'), 'Hide Empty off was not saved')
            capture('settings-show-empty')
            move('down', 7); key('right')
            wait(lambda: 'Settings: RETROARCH_AUTO_RESUME = on' in read('ui-return.log'), 'Auto Resume on was not saved')
            capture('auto-resume-on'); key('left')
            wait(lambda: 'Settings: RETROARCH_AUTO_RESUME = off' in read('ui-return.log'), 'Auto Resume off was not saved')
            capture('auto-resume-off'); key('alt')
            shown = re.findall(r'Home: entry=(\S+) games=(\d+) hide_empty=0', read('ui-return.log'))
            ids = [identity for identity, _ in shown]
            protected = {'__applications', '__opendingux', '__recents', '__favorites', '__settings', '__retroarch', '__explorer', '__exit'}
            assert 'snes' in ids and protected.issubset(ids), ids
            difference = ids.index('__settings') - ids.index('snes')
            move('left', difference); capture('home-empty-snes'); key('ctrl'); capture('empty-snes'); key('alt')
            move('right', difference); key('ctrl'); move('down', 29); key('right')
            wait(lambda: 'Library: hide empty systems=1' in read('ui-return.log'), 'Hide Empty on was not saved')
            capture('settings-hide-empty'); key('alt'); capture('home-hidden-again')
            # All zero-game utilities remain reachable after filtering.
            key('left'); key('ctrl'); capture('opendingux'); key('alt')
            move('left', 5); key('ctrl')
            wait(lambda: 'Applications: catalog ready' in read('ui-return.log'), 'Applications was hidden')
            capture('applications'); key('alt'); key('right'); key('ctrl')
            wait(lambda: 'RecentGames: loaded 1 entries' in read('ui-return.log'), 'Recents was hidden')
            options(); favorite_change('Removed from Favorites'); key('alt')
            options(); favorite_change('Added to Favorites'); key('alt'); key('alt'); key('right'); enter_favorites(1)
            capture('favorites-final'); key('power')
            wait(lambda: 'UI_VM_PHASE_return=0' in read('serial.log'), 'UI did not exit cleanly')
            wait(lambda: 'reboot: System halted' in read('serial.log'), 'Guest did not halt')
        assert not re.search(r'Out of memory|oom-kill|Killed process', log() + read('serial.log'))
        assert re.search(r'SwapTotal:\s+0\s+kB', read('memory.txt'))
        assert log().count('Library: boot scan') == 1 and 'Library: manual scan' not in log(), 'Preference triggered ROM rescan'
        assert read('recents-return.txt') == read('recents-edit.txt'), 'Favorites changes altered session history'
        assert not re.search(r'^RETROARCH_AUTO_RESUME=', read('config-edit.txt'), re.M), 'Auto Resume was not default-off/unset'
        assert re.search(r'^RETROARCH_AUTO_RESUME=off$', read('config-return.txt'), re.M), 'Auto Resume off was not persisted'
        header = shlex.split(read('favorites-return.txt').splitlines()[0])
        assert header == ['LILSMOKE_FAVORITES_1', '1', '1'], header
        record = shlex.split(read('favorites-return.txt').splitlines()[1])
        assert record[1] == '/storage/roms/nes/game.nes', record
        report.update(passed=True, favorites_add_remove=True, favorites_persist_after_game=True, missing_removal=True,
                      hide_empty_toggle=True, utility_entries_preserved=True, no_preference_rescan=True,
                      session_history_unchanged_by_favorites=True, ready_marker=True,
                      retroarch_auto_resume_setting_persisted=True, retroarch_auto_resume_default_off=True,
                      screenshots={name: sha(args.out / (name + '.png')) for name in captures})
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['events'] = events
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        if qmp:
            qmp.close()
        if vm is not None and vm.poll() is None:
            vm.terminate()
            try:
                vm.wait(timeout=10)
            except subprocess.TimeoutExpired:
                vm.kill(); vm.wait()
        root.unlink(missing_ok=True)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
