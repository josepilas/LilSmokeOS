#!/usr/bin/env python3
"""Production UI/cache and stock-supervisor loop in a one-CPU, 128 MiB MIPS VM.

Only stock chroot/display/mount/power hooks are fixture boundaries. A labeled
fixture engine replaces gameplay. No TreeFrog runtime, SD, or SDK writes.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

P = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(P / 'tools/hwtest'))
sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
from mips_boot_smoke import Newc
from mips_disk_fixture import disk_devices, overlay_initramfs, runtime_libraries
from vm_scene_smoke import Qmp
from PIL import Image

PHASES = ['cold', 'warm', 'plain', 'invalid', 'deleted', 'changed', 'off', 'offreturn']
CONFIG = ('DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\nSCAN_ROMS_ON_BOOT=on\n'
          'DEFAULT_THEME=default\nPOINTER_ENABLED=off\nUI_RENDER_MODE=native\n'
          'EMULATOR_BACKEND=retroarch\nSHOW_CLOCK=off\nSHOW_BATTERY=off\n')
ENGINE = '''#!/bin/sh
case "$1" in
resolve) echo ra:fceumm ;;
run)
    phase=$(cat /bench/current-phase)
    env > /bench/engine-$phase.env
    cat /proc/$$/maps > /bench/engine-$phase.maps
    original=$(cat /bench/ui-$phase.pid)
    cat /proc/$original/maps > /bench/handoff-$phase.maps
    cat /proc/meminfo > /bench/engine-$phase.memory
    cat /run/lilsmoke-ui-app-handoff > /bench/engine-$phase.receipt
    echo fixture > /bench/engine-$phase.ready
    while [ ! -f /bench/finish-app-$phase ]; do sleep 0.05; done
    [ "$phase" != deleted ] || exit 9
    exit 0 ;;
*) exit 2 ;;
esac
'''
BENCH = '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
set -x
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
mkdir -p /bench /persist/config /persist/logs /dev/pts /dev/shm /run /storage
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t tmpfs -o size=1m tmpfs /run || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t tmpfs -o size=16m tmpfs /storage || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L persist /persist || poweroff -f
mkdir -p /persist/scrape
mount -t tmpfs -o size=1m tmpfs /persist/scrape || poweroff -f
mkdir -p /storage/roms/nes
set +x
index=10000
while [ "$index" -lt 18584 ]; do
    : > /storage/roms/nes/Game${index#1}.nes
    index=$((index + 1))
done
set -x
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_AUDIO_OVERRIDE=dummy LILSMOKE_UI_PROFILE=1 LILSMOKE_UI_IDLE_REUSE=1
export LILSMOKE_BOOT_READY_FILE=/run/lilsmoke-ui-ready
export LILSMOKE_EMULATOR_ROUTER=/usr/bin/fixture-router LILSMOKE_ROM_RESOLVER=/usr/bin/fixture-resolver
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export LILSMOKE_THEME_TRANSITION=instant
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_STOCK_SUPERVISOR=1
# Deliberately inherited stale hints: production loop initialization must discard them.
export LILSMOKE_UI_WARM_RETURN=1 LILSMOKE_UI_SUPERVISOR_TOKEN=123:456
ROOT=
BASE=/bench
LOCK=/run/qa-lock
loading_dir=/run/qa-loading
loading_disabled=yes
logo_minimum=0
mkdir -p "$LOCK" "$loading_dir"
fallback() { echo "UNEXPECTED_FALLBACK $*"; poweroff -f; }
# Run the exact production restart loop below. Only the stock-root chroot and
# HCFB preload are replaced here, so Malta uses the real SDL DirectFB backend.
chroot() {
    shift
    shift
    shift
    invocation=$(cat /bench/invocations 2>/dev/null || echo 0)
    invocation=$((invocation + 1))
    echo "$invocation" > /bench/invocations
    phase=$(sed -n "${invocation}p" /bench/phases.txt)
    [ -n "$phase" ] || { echo VM_ALL_PHASES_DONE; poweroff -f; exit 1; }
    echo "$phase" > /bench/current-phase
    printf '%s\n' "$@" > /bench/invocation-$phase.env
    rm -f /run/lilsmoke-ui-ready
    cat /proc/uptime > /bench/start-$phase.txt
    /usr/bin/env "$@" > /bench/ui-$phase.log 2>&1 &
    ui=$!
    echo "$ui" > /bench/ui-$phase.pid
    (
        while kill -0 "$ui" 2>/dev/null; do
            if [ -f /bench/request.sh ]; then
                mv /bench/request.sh /run/qa-request.sh
                /bin/sh /run/qa-request.sh
            fi
            if [ ! -f /storage/roms/nes/Game0000.nes ]; then
                echo yes > /bench/rom0-missing-$phase.txt
            fi
            if [ -f /run/lilsmoke-ui-ready ] && [ ! -f /bench/ready-$phase.txt ]; then
                cat /proc/uptime > /bench/ready-uptime-$phase.txt
                cat /proc/$ui/maps > /bench/ui-$phase.maps
                cat /proc/$ui/status > /bench/ui-$phase.status
                cp /run/lilsmoke-ui-ready /bench/ready-$phase.txt
            fi
            if [ -f /bench/stop-ui-$phase ]; then kill -TERM "$ui"; break; fi
            sleep 0.05
        done
    ) &
    watcher=$!
    wait "$ui"
    result=$?
    kill "$watcher" 2>/dev/null || true
    wait "$watcher" 2>/dev/null || true
    echo "$result" > /bench/exit-$phase.txt
    test ! -e /proc/$ui && echo yes > /bench/reaped-$phase.txt
    return "$result"
}
set +x
PRODUCTION_LOOP
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    parser.add_argument('--qemu', type=Path, default=Path('/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel'))
    args = parser.parse_args()
    args.binary, args.out = args.binary.resolve(), args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    supervisor = P / 'board/segam-m8-v6-stock/m8-launch.sh'
    loop = supervisor.read_text().split('runtime_failures=0\n# No inherited', 1)[1]
    loop = 'runtime_failures=0\n# No inherited' + loop
    bench = BENCH.replace('PRODUCTION_LOOP', loop)
    (args.out / 'bench-init.sh').write_text(bench)
    (args.out / 'phases.txt').write_text('\n'.join(PHASES) + '\n')
    report = dict(passed=False, actual_ui_sha256=sha(args.binary), supervisor_sha256=sha(supervisor),
                  launcher_sha256=sha(supervisor),
                  exact_production_restart_loop=True, fixture_engine_not_gameplay=True,
                  stock_chroot_display_mount_power_hooks_qualified=False, physical_m8=False,
                  ram_mib=128, cpus=1, swap_kib=0, sdk_writes=False, sd_access=False,
                  events=[], screenshots={}, phases={})
    vm = qmp = None

    def read(name):
        path = args.out / name
        return path.read_text(errors='replace') if path.exists() else ''

    try:
        with tempfile.TemporaryDirectory(prefix='lilsmoke-warm-vm-') as temporary:
            work = Path(temporary)
            persist = work / 'persist'
            (persist / 'config').mkdir(parents=True)
            config = persist / 'config/lilsmoke.conf'
            config.write_text(CONFIG)
            overlay = Newc()
            overlay.file('usr/bin/lilsmoke-ui', args.binary.read_bytes(), 0o755)
            session = args.binary.parent / 'lilsmoke-session'
            overlay.file('usr/lib/lilsmoke/lilsmoke-session', session.read_bytes(), 0o755)
            overlay.file('usr/lib/lilsmoke/lilsmoke-launch', (P / 'package/lilsmoke-scripts/src/lilsmoke-launch').read_bytes(), 0o755)
            overlay.file('usr/lib/libretro/fceumm_libretro.so', 'fixture availability, not a core\n')
            overlay.file('usr/bin/fixture-router', ENGINE, 0o755)
            overlay.file('usr/bin/fixture-resolver', '#!/bin/sh\nprintf "%s\\n" "$2"\n', 0o755)
            overlay.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
            overlay.file('etc/lilsmoke/input-maps/default.map', (P / 'board/qemu-mipsel/input-maps/default.map').read_bytes())
            overlay.file('usr/bin/bench-init', bench, 0o755)
            runtime_libraries(args.sdk, overlay, [args.binary, session])
            initrd = work / 'boot.cpio.gz'
            initrd.write_bytes(gzip.compress(overlay_initramfs(args.sdk, overlay, '/usr/bin/bench-init', overlay_mib=16, trace=True)))
            command = [str(args.qemu), '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '128',
                       '-kernel', str(args.sdk / 'images/vmlinux'), '-initrd', str(initrd),
                       '-append', 'console=ttyS0 video=cirrusfb:640x480M-24@60 panic=-1',
                       '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
                       '-no-reboot', '-device', 'usb-kbd', '-qmp', f'unix:{work}/qmp.sock,server=on,wait=off']
            command += disk_devices(args.sdk, [('bench', args.out, False), ('persist', persist, False)])
            report.update(kernel_sha256=sha(args.sdk / 'images/vmlinux'), rootfs_reused_readonly=True,
                          rootfs_sha256=sha(args.sdk / 'images/rootfs.squashfs'))
            (args.out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
            with (args.out / 'serial.log').open('wb') as serial:
                vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=serial, stderr=subprocess.STDOUT)

                def wait(predicate, message, seconds=180):
                    deadline = time.monotonic() + seconds
                    while not predicate():
                        if vm.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError(message + '\n' + read('serial.log')[-2500:] + '\n' +
                                               read('ui-' + read('current-phase').strip() + '.log')[-2500:])
                        time.sleep(.15)

                def ready(phase):
                    wait(lambda: read('ready-' + phase + '.txt') == 'UI_FIRST_SUBMITTED_FRAME\n', 'No UI ready: ' + phase)
                    log = read('ui-' + phase + '.log')
                    started = float(read('start-' + phase + '.txt').split()[0])
                    submitted = float(read('ready-uptime-' + phase + '.txt').split()[0])
                    report['phases'][phase] = dict(start_to_ready_guest_seconds=round(submitted - started, 3),
                                                  cached='using cached game index' in log,
                                                  scanned='Library: boot scan' in log,
                                                  retained='unchanged warm playlists retained' in log,
                                                  invocation=read('invocation-' + phase + '.env'))
                    print(phase, report['phases'][phase], flush=True)

                ready('cold')
                qmp = Qmp(work / 'qmp.sock')

                def key(code, delay=.7):
                    qmp.call('send-key', dict(keys=[dict(type='qcode', data=code)], **{'hold-time': 80}))
                    report['events'].append(code)
                    time.sleep(delay)

                def home_entry(phase, entry):
                    entries = re.findall(r'Home: entry=(\S+)', read('ui-' + phase + '.log'))
                    # Every caller begins at the freshly constructed root (selected zero).
                    entries = entries[-len(set(entries)):]
                    for _ in range(entries.index(entry)):
                        key('right', .4)
                    key('ctrl')

                def capture(name):
                    ppm = work / (name + '.ppm')
                    qmp.call('screendump', dict(filename=str(ppm)))
                    with Image.open(ppm) as im:
                        colors = len(im.getcolors(im.width * im.height) or [])
                        assert im.size == (640, 480) and colors > 8
                        im.save(args.out / (name + '.png'))
                    report['screenshots'][name] = dict(colors=colors, sha256=sha(args.out / (name + '.png')))

                def launch(phase):
                    key('ctrl')
                    wait(lambda: 'GameList: ready for nes' in read('ui-' + phase + '.log'), 'No game list: ' + phase)
                    key('ctrl')
                    wait(lambda: read('engine-' + phase + '.ready'), 'No exec handoff: ' + phase)
                    assert 'lilsmoke-ui' not in read('engine-' + phase + '.maps')
                    assert 'libSDL' not in read('engine-' + phase + '.maps')
                    assert 'lilsmoke-ui' not in read('handoff-' + phase + '.maps')
                    assert 'libSDL' not in read('handoff-' + phase + '.maps')
                    assert 'LILSMOKE_UI_WARM_RETURN=' not in read('engine-' + phase + '.env')
                    assert 'LILSMOKE_UI_SUPERVISOR_TOKEN=' not in read('engine-' + phase + '.env')
                    assert re.fullmatch(r'\d+:\d+\n', read('engine-' + phase + '.receipt'))
                    assert re.search(r'SwapTotal:\s+0\s+kB', read('engine-' + phase + '.memory'))

                def finish_app(phase):
                    (args.out / ('finish-app-' + phase)).touch()
                    wait(lambda: read('reaped-' + phase + '.txt') == 'yes\n', 'UI/app not reaped')

                def stop_ui(phase):
                    (args.out / ('stop-ui-' + phase)).touch()
                    wait(lambda: read('reaped-' + phase + '.txt') == 'yes\n', 'UI not reaped')

                action_number = 0
                def action(script):
                    nonlocal action_number
                    action_number += 1
                    temporary = args.out / 'request.pending'
                    temporary.write_text(script + f'\necho {action_number} > /bench/request-done.txt\n')
                    temporary.replace(args.out / 'request.sh')
                    wait(lambda: read('request-done.txt').strip() == str(action_number), 'Guest fixture action failed')

                assert report['phases']['cold']['scanned'] and not report['phases']['cold']['cached']
                capture('01-cold-home')
                initial_cache = sha(persist / 'cache/roms.index')
                initial_playlist = sha(persist / 'playlists/LilSmoke-nes.lpl')
                launch('cold'); finish_app('cold')
                ready('warm')
                assert report['phases']['warm']['cached'] and report['phases']['warm']['retained']
                assert sha(persist / 'cache/roms.index') == initial_cache
                assert sha(persist / 'playlists/LilSmoke-nes.lpl') == initial_playlist
                capture('02-warm-home')
                # Exercise real Settings -> Scan games now while boot scan is on.
                # Keep the mutation/fallback cases small after proving both 8584-ROM
                # startup paths. This avoids repeating a large synthetic 9p scan.
                action('find /storage/roms/nes -type f ! -name Game0000.nes ! -name Game0001.nes '
                       '! -name Game0002.nes -delete\n: > /storage/roms/nes/ManualAdded.nes')
                home_entry('warm', '__settings')
                key('down'); key('ctrl')
                wait(lambda: 'Library: manual scan' in read('ui-warm.log') and
                     'Library: 4 games ready' in read('ui-warm.log'), 'Manual scan did not refresh')
                assert 'ManualAdded.nes' in (persist / 'playlists/LilSmoke-nes.lpl').read_text()
                wait(lambda: read('ui-warm.log').count('SystemView: first frame complete') >= 2,
                     'Manual scan root frame did not submit')
                capture('03-manual-refresh')
                # Real Explorer deletion: enter /storage/roms/nes and remove first ROM.
                home_entry('warm', '__explorer')
                action('ls -1a /storage > /bench/storage-list.txt\n'
                       'test ! -f /run/lilsmoke-ui-library-dirty || echo yes > /bench/explorer-entered.txt')
                assert read('explorer-entered.txt') == 'yes\n', 'Explorer did not open'
                directories = sorted(name for name in read('storage-list.txt').splitlines() if name not in ('.', '..'))
                for _ in range(directories.index('roms')): key('down', .2)
                key('ctrl'); key('ctrl'); key('spc')
                for _ in range(5): key('down', .18)
                key('ctrl'); key('ctrl')
                wait(lambda: read('rom0-missing-warm.txt') == 'yes\n', 'Explorer delete did not apply')
                key('alt'); key('alt'); key('alt')
                wait(lambda: read('ui-warm.log').count('Library: manual scan') == 2 and
                     'Library: 3 games ready' in read('ui-warm.log').rsplit('Library: manual scan', 1)[-1],
                     'Explorer close did not rescan')
                assert 'Game0000.nes' not in (persist / 'playlists/LilSmoke-nes.lpl').read_text()
                wait(lambda: read('ui-warm.log').count('SystemView: first frame complete') >= 3,
                     'Explorer refresh root frame did not submit')
                capture('04-explorer-refreshed')
                stop_ui('warm')
                ready('plain')
                assert report['phases']['plain']['scanned'] and not report['phases']['plain']['cached']
                assert 'LILSMOKE_UI_WARM_RETURN=0\n' in read('invocation-plain.env')
                launch('plain')
                (persist / 'cache/roms.index').write_text('corrupt fixture\n')
                finish_app('plain'); ready('invalid')
                assert report['phases']['invalid']['scanned'] and not report['phases']['invalid']['cached']
                launch('invalid')
                action('rm /storage/roms/nes/Game0001.nes')
                finish_app('invalid'); ready('deleted')
                assert report['phases']['deleted']['scanned'] and not report['phases']['deleted']['cached']
                assert 'Game0001.nes' not in (persist / 'playlists/LilSmoke-nes.lpl').read_text()
                launch('deleted')
                config.write_text(CONFIG + 'EMULATOR_nes=ra:fceumm\n')
                (persist / 'playlists/LilSmoke-nes.lpl').write_text('damaged playlist\n')
                finish_app('deleted'); ready('changed')
                assert report['phases']['changed']['cached'] and not report['phases']['changed']['retained']
                assert json.loads((persist / 'playlists/LilSmoke-nes.lpl').read_text())['items']
                assert read('exit-deleted.txt') == '9\n', 'Application-failure return was not exercised'
                config.write_text(CONFIG.replace('SCAN_ROMS_ON_BOOT=on', 'SCAN_ROMS_ON_BOOT=off'))
                stop_ui('changed'); ready('off')
                assert report['phases']['off']['cached'] and not report['phases']['off']['retained']
                launch('off'); finish_app('off'); ready('offreturn')
                assert report['phases']['offreturn']['cached'] and report['phases']['offreturn']['retained']
                capture('05-off-warm-home')
                stop_ui('offreturn')
                wait(lambda: 'VM_ALL_PHASES_DONE' in read('serial.log') and
                     ('reboot: System halted' in read('serial.log') or vm.poll() is not None),
                     'Guest fixture did not finish and halt', 30)
                assert 'VM_ALL_PHASES_DONE' in read('serial.log')
                assert not re.search(r'Out of memory|oom-kill|Killed process|segfault', read('serial.log'), re.I)
                report.update(passed=True, fixture_rom_count=8584, mutation_fixture_rom_count=4, manual_scan_refreshed=True,
                              explorer_delete_refreshed=True, failed_app_return=True, no_resident_ui=True,
                              cache_corruption_fallback=True, deleted_rom_fallback=True,
                              cold_boot_setting_preserved=True, playlist_change_refreshed=True,
                              true_cold_scan_preserved=True, warm_cache_used=True,
                              manual_scan_preserved=True, invalid_cache_fallback=True,
                              guest_fixture_halted=True,
                              rom_fixture_filesystem='guest tmpfs, 16 MiB cap; zero-byte named fixtures, not ROM content')
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        if qmp:
            qmp.close()
        if vm and vm.poll() is None:
            vm.terminate()
            try:
                vm.wait(timeout=5)
            except subprocess.TimeoutExpired:
                vm.kill(); vm.wait(timeout=5)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
