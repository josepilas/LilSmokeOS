#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Actual MIPS UI navigation with a guest-overlay recording launcher, never gameplay."""
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

sys.dont_write_bytecode = True
P = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(P / 'tools/hwtest'))
sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
from mips_boot_smoke import Newc
from mips_disk_fixture import disk_devices, overlay_initramfs, runtime_libraries
from vm_scene_smoke import Qmp
from PIL import Image

RECORDING_HELPER = '''#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# QA overlay only. Deliberately does not run a port, engine, or original script.
prefix=handoff
[ "$LILSMOKE_TREEFROG_QA_PHASE" != return ] || prefix=return-handoff
printf '%s\n' "$#" > /bench/$prefix.argc
printf '%s\n' "$@" > /bench/$prefix.argv
printf '%s\n' "$$" > /bench/$prefix.pid
readlink /proc/$$/exe > /bench/$prefix.exe
ls -l /proc/$$/fd > /bench/$prefix-fds.txt
cat /proc/$$/maps > /bench/$prefix-maps.txt
cat /proc/$$/status > /bench/$prefix-status.txt
echo TREEFROG_RECORDING_HELPER_ONLY
[ "$#" -eq 1 ] && [ "$1" = hexahop ] || exit 9
mkdir -p /run/m8-loading
rm -f /run/m8-loading/stopped
printf 'fixture-token\n' > /run/m8-boot-logo.state
: > /run/m8-loading/active
# Token-only fake logo: no M8 display, preload, original port, or engine API.
(
    while [ -e /run/m8-loading/active ]; do sleep 0.01; done
    : > /run/m8-loading/stopped
) &
logo=$!
export LILSMOKE_M8_BOOT_STATE=/run/m8-boot-logo.state LILSMOKE_M8_LOADING_GAME=1
export LILSMOKE_M8_LOADING_DIR=/run/m8-loading LILSMOKE_LOADING_PROGRESS=/run/m8-loading/progress
export TREEFROG_QA_PREFIX="$prefix"
/usr/lib/lilsmoke/lilsmoke-treefrog-input --engine /run/lilsmoke-treefrog-root /engine.sh
status=$?
[ "$status" -eq 0 ] || kill "$logo"
wait "$logo"
[ "$status" -eq 0 ] && [ ! -e /run/m8-loading/active ] &&
    [ ! -e /run/m8-boot-logo.state ] && [ -e /run/m8-loading/stopped ] || exit 10
echo yes > /bench/$prefix-loading-released.txt
exit 0
'''

MOCK_ENGINE = '''#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The production bridge has already released the mock logo before this exec.
[ -z "${LILSMOKE_M8_BOOT_STATE+x}${LILSMOKE_M8_LOADING_GAME+x}${LILSMOKE_M8_LOADING_DIR+x}${LILSMOKE_LOADING_PROGRESS+x}" ] || exit 11
[ ! -e /loading/active ] && [ -e /loading/stopped ] || exit 12
echo mock-engine-entry > /bench/$TREEFROG_QA_PREFIX-engine.txt
exit 0
'''

BENCH_INIT = '''#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
exec </dev/console >/dev/console 2>&1
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
mkdir -p /bench /persist/config /persist/logs /dev/pts /dev/shm /run /storage/treefrog-ports
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t tmpfs -o size=1m tmpfs /run || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,ro,nosuid,nodev,noexec ports /storage/treefrog-ports || poweroff -f
view=/run/lilsmoke-treefrog-root
mkdir -p "$view/bin" "$view/lib" "$view/bench" "$view/loading" /run/m8-loading
mount --bind /bin "$view/bin" || poweroff -f
mount --bind /lib "$view/lib" || poweroff -f
mount --bind /bench "$view/bench" || poweroff -f
mount --bind /run/m8-loading "$view/loading" || poweroff -f
cp /usr/lib/lilsmoke/qa-engine.sh "$view/engine.sh"
cat /proc/mounts > /bench/mounts.txt
cat /proc/meminfo > /bench/memory-before.txt
cat /proc/swaps > /bench/swaps.txt
sha256sum /storage/treefrog-ports/hexahop/config.json /storage/treefrog-ports/hexahop/run.sh /storage/treefrog-ports/hexahop/hex-a-hop.mipsel > /bench/original-before.sha256
unset LILSMOKE_HCFB LILSMOKE_EXPLORER_RETURN LILSMOKE_EXPLORER_FOCUS
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_AUDIO_OVERRIDE=dummy LILSMOKE_UI_PROFILE=1 LILSMOKE_UI_IDLE_REUSE=1
export LILSMOKE_BOOT_READY_FILE=/run/lilsmoke-ui-ready
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export LILSMOKE_THEME_TRANSITION=instant
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force
export LILSMOKE_TREEFROG_QA_PHASE=launch
/usr/bin/lilsmoke-ui > /bench/ui.log 2>&1 &
ui_pid=$!
echo "$ui_pid" > /bench/ui.pid
while [ ! -f /run/lilsmoke-ui-ready ] && kill -0 "$ui_pid" 2>/dev/null; do sleep 0.1; done
if [ -f /run/lilsmoke-ui-ready ]; then
    cp /run/lilsmoke-ui-ready /bench/ready.txt
    cat /proc/$ui_pid/status > /bench/ui-status.txt
    cat /proc/$ui_pid/maps > /bench/ui-maps.txt
    ls -l /proc/$ui_pid/fd > /bench/ui-fds.txt
fi
while [ ! -f /bench/capture-catalogue ] && kill -0 "$ui_pid" 2>/dev/null; do sleep 0.1; done
cat /proc/$ui_pid/status > /bench/ui-catalogue-status.txt
cat /proc/meminfo > /bench/memory-catalogue.txt
wait "$ui_pid"
status=$?
echo "$status" > /bench/ui-exit.txt
echo TREEFROG_UI_EXIT=$status
cat /proc/meminfo > /bench/memory-after-handoff.txt
test ! -e /proc/$ui_pid && echo yes > /bench/old-ui-reaped.txt
# Guest-only supervisor: verify reacquisition after exec without claiming runtime integration.
rm -f /run/lilsmoke-ui-ready
export LILSMOKE_TREEFROG_QA_PHASE=return
/usr/bin/lilsmoke-ui > /bench/ui-return.log 2>&1 &
ui_pid=$!
echo "$ui_pid" > /bench/ui-return.pid
while [ ! -f /run/lilsmoke-ui-ready ] && kill -0 "$ui_pid" 2>/dev/null; do sleep 0.1; done
if [ -f /run/lilsmoke-ui-ready ]; then
    cp /run/lilsmoke-ui-ready /bench/ready-return.txt
    cat /proc/$ui_pid/status > /bench/ui-return-status.txt
    cat /proc/$ui_pid/maps > /bench/ui-return-maps.txt
    ls -l /proc/$ui_pid/fd > /bench/ui-return-fds.txt
    cat /proc/meminfo > /bench/memory-return.txt
fi
wait "$ui_pid"
status=$?
echo "$status" > /bench/ui-return-exit.txt
echo TREEFROG_UI_RETURN_EXIT=$status
sha256sum /storage/treefrog-ports/hexahop/config.json /storage/treefrog-ports/hexahop/run.sh /storage/treefrog-ports/hexahop/hex-a-hop.mipsel > /bench/original-after.sha256
cat /proc/meminfo > /bench/memory-after.txt
umount "$view/loading"
umount "$view/bench"
umount "$view/lib"
umount "$view/bin"
sync
poweroff -f
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def manifest(root):
    return {str(path.relative_to(root)): sha(path) for path in sorted(root.rglob('*')) if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--bridge', type=Path, default=P / 'tools/treefrog-compat/_build/mips/lilsmoke-treefrog-input')
    parser.add_argument('--ports', type=Path, default=P / 'tools/treefrog-compat/_build/hexahop-original-20261027/roms/ports')
    parser.add_argument('--sdk', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    parser.add_argument('--qemu', type=Path, default=Path('/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel'))
    args = parser.parse_args()
    args.binary, args.out, args.ports = args.binary.resolve(), args.out.resolve(), args.ports.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    original = args.ports / 'hexahop'
    before = manifest(original)
    for name in ('config.json', 'run.sh', 'hex-a-hop.mipsel', 'boxart.png'):
        if name not in before:
            raise RuntimeError('Missing original Hex-a-hop release file: ' + name)
    metadata = json.loads((original / 'config.json').read_text())
    assert metadata['id'] == 'hexahop' and metadata['launch']['type'] == 'native'
    report = dict(passed=False, qualification='Actual production UI navigation and exec handoff only',
                  original_engine_executed=False, native_gameplay_qualified=False,
                  libretro_gameplay_qualified=False, recording_helper_stage_only=True,
                  return_supervisor_stage_only=True, production_return_integration_qualified=False,
                  m8_loading_fixture_only=True, actual_m8_scanout_qualified=False,
                  actual_ui_sha256=sha(args.binary), ram_mib=128, cpus=1, swap_kib=0,
                  sd_access=False, sdk_modified=False, sdk_global_rebuild=False,
                  original_release=str(original), original_manifest_before=before,
                  physical_hc16xx=False, hardware_3d=False, physical_smoothness_qualified=False)
    events = []
    screenshots = {}
    vm = qmp = None

    def read(name):
        path = args.out / name
        return path.read_text(errors='replace') if path.exists() else ''

    try:
        with tempfile.TemporaryDirectory(prefix='lilsmoke-treefrog-ui-vm-') as temporary:
            work = Path(temporary)
            fixture = Newc()
            fixture.file('usr/bin/lilsmoke-ui', args.binary.read_bytes(), 0o755)
            fixture.file('usr/lib/lilsmoke/lilsmoke-treefrog-launch', RECORDING_HELPER, 0o755)
            fixture.file('usr/lib/lilsmoke/lilsmoke-treefrog-input', args.bridge.read_bytes(), 0o755)
            fixture.file('usr/lib/lilsmoke/qa-engine.sh', MOCK_ENGINE, 0o755)
            fixture.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
            fixture.file('etc/lilsmoke/input-maps/default.map',
                         (P / 'board/qemu-mipsel/input-maps/default.map').read_bytes())
            fixture.file('persist/config/lilsmoke.conf',
                         'DISPLAY_WIDTH=640\nDISPLAY_HEIGHT=480\nSCAN_ROMS_ON_BOOT=off\n'
                         'DEFAULT_THEME=default\nPOINTER_ENABLED=off\nUI_RENDER_SCALE=native\n'
                         'EMULATOR_BACKEND=retroarch\nSHOW_CLOCK=off\nSHOW_BATTERY=off\n')
            fixture.file('usr/bin/bench-init', BENCH_INIT, 0o755)
            runtime_libraries(args.sdk, fixture, [args.binary, args.bridge])
            overlay_bytes = sum(len(entry.data) for entry in fixture.entries)
            if overlay_bytes > 14 * 1024 * 1024:
                raise RuntimeError('Diagnostic overlay exceeds 14 MiB budget')
            initrd = work / 'boot.cpio.gz'
            initrd.write_bytes(gzip.compress(overlay_initramfs(args.sdk, fixture, '/usr/bin/bench-init', overlay_mib=16)))
            (args.out / 'recording-helper.sh.txt').write_text(RECORDING_HELPER)
            (args.out / 'bench-init.sh.txt').write_text(BENCH_INIT)
            command = [str(args.qemu), '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '128',
                       '-kernel', str(args.sdk / 'images/vmlinux'), '-initrd', str(initrd),
                       '-append', 'console=ttyS0 video=cirrusfb:640x480M-24@60 panic=-1',
                       '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
                       '-no-reboot', '-device', 'usb-kbd',
                       '-qmp', f'unix:{work}/qmp.sock,server=on,wait=off']
            command += disk_devices(args.sdk, [('bench', args.out, False), ('ports', args.ports, True)])
            (args.out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
            report.update(kernel_sha256=sha(args.sdk / 'images/vmlinux'),
                          rootfs_sha256=sha(args.sdk / 'images/rootfs.squashfs'),
                          rootfs_reused_readonly=True, overlay_bytes=overlay_bytes,
                          production_bridge_sha256=sha(args.bridge),
                          initrd_bytes=initrd.stat().st_size, initrd_sha256=sha(initrd),
                          recording_helper_sha256=hashlib.sha256(RECORDING_HELPER.encode()).hexdigest())
            with (args.out / 'serial.log').open('wb') as serial:
                vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=serial, stderr=subprocess.STDOUT)

                def wait(predicate, name, seconds=120):
                    deadline = time.monotonic() + seconds
                    while not predicate():
                        if vm.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError(name + '\n' + read('serial.log')[-2400:] + '\n' + read('ui.log')[-2400:])
                        time.sleep(.25)

                wait(lambda: 'SystemView: first frame complete' in read('ui.log') and read('ready.txt'),
                     'Actual UI did not submit Home frame')
                assert read('ready.txt') == 'UI_FIRST_SUBMITTED_FRAME\n'
                assert 'ROMScanner: 0 ROMs found' in read('ui.log'), 'Original ports leaked into the ROM catalogue'
                qmp = Qmp(work / 'qmp.sock')

                def key(code):
                    qmp.key(code)
                    events.append(code)
                    time.sleep(1)

                def capture(name):
                    ppm = work / (name + '.ppm')
                    qmp.call('screendump', dict(filename=str(ppm)))
                    target = args.out / (name + '.png')
                    with Image.open(ppm) as image:
                        assert image.size == (640, 480), image.size
                        colors = len(image.getcolors(image.width * image.height) or [])
                        assert colors > 8, 'Blank UI frame'
                        image.save(target)
                    screenshots[name] = dict(sha256=sha(target), bytes=target.stat().st_size,
                                             width=640, height=480, colors=colors)

                time.sleep(2)
                capture('01-home')
                entries = re.findall(r'Home: entry=(\S+)', read('ui.log'))
                assert '__treefrog' in entries
                for _ in range(entries.index('__treefrog')):
                    key('right')
                capture('02-home-treefrog-selected')
                key('ctrl')
                ready = 'TreeFrog Ports: catalogue ready entries=1 root=/storage/treefrog-ports'
                wait(lambda: ready in read('ui.log'), 'TreeFrog list did not load the original release')
                capture('03-original-hexahop-list')
                key('alt')
                capture('04-home-return')
                key('ctrl')
                wait(lambda: read('ui.log').count(ready) == 2, 'TreeFrog re-entry did not rescan')
                capture('05-original-hexahop-reentry')
                (args.out / 'capture-catalogue').touch()
                wait(lambda: re.search(r'VmHWM:\s+\d+', read('ui-catalogue-status.txt')),
                     'Catalogue memory sample missing')
                key('ctrl')
                wait(lambda: read('ui-exit.txt') == '0\n', 'Actual UI recording handoff failed')
                wait(lambda: read('ready-return.txt') == 'UI_FIRST_SUBMITTED_FRAME\n' and
                     'SystemView: first frame complete' in read('ui-return.log'),
                     'Actual UI did not return after recording helper')
                time.sleep(2)
                capture('06-home-after-handoff')
                assert read('old-ui-reaped.txt') == 'yes\n'
                assert read('ui-return.pid') != read('ui.pid')
                for _ in range(entries.index('__treefrog')):
                    key('right')
                key('ctrl')
                wait(lambda: ready in read('ui-return.log'), 'Returned UI did not reopen original catalogue')
                capture('07-original-hexahop-after-return')
                key('ctrl')
                wait(lambda: read('ui-return-exit.txt') == '0\n', 'Returned UI handoff failed')
                wait(lambda: 'reboot: System halted' in read('serial.log'), 'Guest did not halt')
                assert read('handoff.argc') == '1\n' and read('handoff.argv') == 'hexahop\n'
                assert read('handoff.pid') == read('ui.pid'), 'Handoff did not replace the original UI process'
                assert 'TreeFrog Ports: handoff id=hexahop type=native' in read('ui.log')
                assert 'TREEFROG_RECORDING_HELPER_ONLY' in read('ui.log')
                assert '/dev/fb0' in read('ui-fds.txt') + read('ui-maps.txt'), 'Actual UI did not own the framebuffer'
                assert not re.search(r'/dev/(fb\d|input/)', read('handoff-fds.txt')), 'UI device descriptors survived handoff'
                assert 'lilsmoke-ui' not in read('handoff-maps.txt'), 'UI mappings survived exec'
                assert '/dev/fb0' in read('ui-return-fds.txt') + read('ui-return-maps.txt')
                assert read('return-handoff.argc') == '1\n' and read('return-handoff.argv') == 'hexahop\n'
                assert read('return-handoff.pid') == read('ui-return.pid')
                assert not re.search(r'/dev/(fb\d|input/)', read('return-handoff-fds.txt'))
                assert 'lilsmoke-ui' not in read('return-handoff-maps.txt')
                for prefix in ('handoff', 'return-handoff'):
                    assert read(prefix + '-loading-released.txt') == 'yes\n', 'Mock loading logo/token lingered'
                    assert read(prefix + '-engine.txt') == 'mock-engine-entry\n'
                assert read('original-before.sha256') == read('original-after.sha256')
                assert re.search(r'SwapTotal:\s+0\s+kB', read('memory-before.txt'))
                assert len(read('swaps.txt').splitlines()) == 1
                mount = next(line for line in read('mounts.txt').splitlines() if ' /storage/treefrog-ports ' in line)
                assert 'ro,' in mount and 'noexec' in mount
                assert not re.search(r'Out of memory|oom-kill|Killed process|segfault',
                                     read('serial.log') + read('ui.log') + read('ui-return.log'), re.I)
                assert screenshots['03-original-hexahop-list']['sha256'] == screenshots['05-original-hexahop-reentry']['sha256']
                assert screenshots['01-home']['sha256'] != screenshots['03-original-hexahop-list']['sha256']
                assert screenshots['01-home']['sha256'] == screenshots['06-home-after-handoff']['sha256']
                assert screenshots['03-original-hexahop-list']['sha256'] == screenshots['07-original-hexahop-after-return']['sha256']
                memory = {phase: {key: int(value) for key, value in
                                  re.findall(r'^(MemAvailable|MemFree|AnonPages|SwapTotal):\s+(\d+)', read(name), re.M)}
                          for phase, name in [('before', 'memory-before.txt'), ('catalogue', 'memory-catalogue.txt'),
                                              ('after_handoff', 'memory-after-handoff.txt'),
                                              ('returned_ui', 'memory-return.txt'), ('after', 'memory-after.txt')]}
                assert all(sample.get('SwapTotal') == 0 and sample.get('MemAvailable', 0) > 16 * 1024
                           for sample in memory.values()), 'Guest memory headroom or no-swap gate failed'
                report.update(passed=True, home_treefrog_list_original_selection=True, freshscan_on_reentry=True,
                              helper_argv=['hexahop'], ui_process_replaced=True, ui_devices_closed_before_handoff=True,
                              ui_exit_status=0, original_release_guest_hashes_unchanged=True,
                              actual_ui_returned=True, return_input_and_second_handoff=True,
                              return_ui_exit_status=0, old_ui_reaped=True, ui_mappings_replaced=True,
                              m8_fixture_loading_released_twice=True, m8_fixture_tokens_removed=True,
                              memory_kib=memory, ui_catalogue_vmhwm_kib=int(re.search(r'VmHWM:\s+(\d+)', read('ui-catalogue-status.txt')).group(1)),
                              ui_return_initial_frame_vmhwm_kib=int(re.search(r'VmHWM:\s+(\d+)', read('ui-return-status.txt')).group(1)),
                              original_mount=mount, home_entries=entries, events=events, screenshots=screenshots,
                              ui_status=read('ui-status.txt'), ui_initial_frame_vmhwm_kib=int(re.search(r'VmHWM:\s+(\d+)', read('ui-status.txt')).group(1)))
    except Exception as error:
        report.update(error=str(error), events=events, screenshots=screenshots)
        raise
    finally:
        if qmp:
            qmp.close()
        if vm and vm.poll() is None:
            vm.terminate()
            try:
                vm.wait(timeout=5)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait(timeout=5)
        report['original_manifest_after'] = manifest(original)
        report['original_release_host_hashes_unchanged'] = report['original_manifest_after'] == before
        if not report['original_release_host_hashes_unchanged']:
            report['passed'] = False
        report['retained_output_bytes'] = sum(p.stat().st_size for p in args.out.rglob('*') if p.is_file())
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items()
                      if key not in ('original_manifest_before', 'original_manifest_after', 'ui_status')}, indent=2))
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
