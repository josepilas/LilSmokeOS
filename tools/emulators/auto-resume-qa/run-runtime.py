# SPDX-License-Identifier: MPL-2.0
"""Installed MIPS RetroArch + real FCEUmm auto-state lifecycle, private QA only."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

P = Path(__file__).resolve().parents[3]
B = Path('/root/lilsmoke-os/build/buildroot/output')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def authored_rom(seed):
    # Authored NROM-128: a 6502 RAM counter, no proprietary game/data.
    rom = bytearray(16 + 16384 + 8192)
    rom[:8] = b'NES\x1a\x01\x01\x00\x00'
    code = bytes([0x78, 0xd8, 0xa2, 0xff, 0x9a, 0xa9, seed, 0x85, 0x00,
                  0xe6, 0x00, 0xd0, 0xfc, 0xe6, 0x01, 0x4c, 0x09, 0x80])
    rom[16:16 + len(code)] = code
    for vector in (0x3ffa, 0x3ffc, 0x3ffe):
        rom[16 + vector:16 + vector + 2] = b'\x00\x80'
    return rom


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--timeout', type=int, default=480)
    ap.add_argument('--direct-core', action='store_true', help='Unwrapped installed core; no observer/probe modes')
    ap.add_argument('--baseline-off', action='store_true', help='Direct core, two launches with option disabled')
    ap.add_argument('--regular-tasks', action='store_true', help='QA only: exercise upstream non-threaded data task queue')
    ap.add_argument('--capture-crash', action='store_true', help='QA guest only: bounded core dumps and host GDB backtraces')
    ap.add_argument('--trace-mutex', action='store_true', help='QA only: observe mutex destruction, never suppress operations')
    args = ap.parse_args()
    if args.baseline_off:
        args.direct_core = True
    q = args.out.resolve()
    q.mkdir(exist_ok=False)
    cpu = min(os.sched_getaffinity(0))
    prefix = ['taskset', '-c', str(cpu), 'nice', '-n', '10']
    cc = B / 'host/bin/mipsel-buildroot-linux-musl-gcc'
    headers = B / 'build/retroarch-mips-v1.19.1/libretro-common/include'
    sources = [P / 'tools/gamepads/qa_fb_present.c', Path(__file__).with_name('observed-core.c')]
    inputs = {str(f): digest(f) for f in [*sources,
        P / 'package/lilsmoke-scripts/src/lilsmoke-retroarch',
        P / 'package/lilsmoke-scripts/src/lilsmoke-config-read',
        B / 'target/usr/bin/retroarch', B / 'target/usr/lib/libretro/fceumm_libretro.so',
        headers / 'libretro.h']}
    (q / 'inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    commands = []
    compile_inputs = list(zip(sources, ('present.so', 'observed.so')))
    if args.trace_mutex:
        compile_inputs.append((Path(__file__).with_name('mutex-trace.c'), 'mutex-trace.so'))
    for source, name in compile_inputs:
        command = prefix + ['prlimit', '--as=805306368', '--', str(cc), '-O2', '-shared',
                            '-fPIC', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(headers), str(source), '-o', str(q / name), '-ldl']
        commands.append(command)
        with (q / (name + '.build.log')).open('w') as log:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    for name, seed in (('Alpha', 1), ('Beta', 19), ('NoState', 37)):
        (q / (name + '.nes')).write_bytes(authored_rom(seed))
    sys.path.insert(0, str(P / 'tools/hwtest'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs, disk_devices, runtime_libraries

    # Each launch is a new frontend process, bounded by its own normal exit path.
    init = r'''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /storage /persist/config /dev/pts /dev/shm /run
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || { poweroff -f; exit 1; }
cat /proc/meminfo > /bench/memory.txt
cat /proc/swaps > /bench/swaps.txt
cat /proc/cpuinfo > /bench/cpu.txt
export HOME=/persist LILSMOKE_HCFB=1 LILSMOKE_M8_AUDIO=0
export LILSMOKE_AUDIO_OVERRIDE=dummy LILSMOKE_DFB_PIXEL_OPTIONS=depth=16,pixelformat=RGB16
export LILSMOKE_SESSION_LOG=/run/not-installed LILSMOKE_LOG_DIR=/bench
export LILSMOKE_SAVE_ROOT=/bench/saves LILSMOKE_STATE_ROOT=/bench/states
export LILSMOKE_CONFIG=/bench/system.conf LILSMOKE_DEFAULT_CONFIG=/bench/default.conf
export LILSMOKE_CONFIG_READER=/usr/lib/lilsmoke/lilsmoke-config-read
export LILSMOKE_ROM_RESOLVER=/usr/lib/lilsmoke/lilsmoke-rom
export LILSMOKE_RETROARCH_USER_CONFIG=/bench/user.cfg
export LILSMOKE_ARCHIVE_CACHE=/tmp/archive-cache
printf 'state_slot = 17
' > /bench/user.cfg
: > /bench/default.conf
configuration() {
    printf 'DISPLAY_WIDTH=856\nDISPLAY_HEIGHT=480\nRETROARCH_AUTO_RESUME=%s\n' "$1" > /bench/system.conf
}
launch() {
    label=$1
    game=$2
    core=${3:-fceumm_libretro.so}
    echo "QA_CASE_START $label"
    /usr/lib/lilsmoke/lilsmoke-retroarch run nes "/bench/$game.nes" "$core"
    result=$?
    cp /bench/retroarch.log "/bench/$label.log"
    cp /tmp/lilsmoke-retroarch/retroarch.cfg "/bench/$label.cfg"
    printf '%s\n' "$result" > "/bench/$label.exit"
    echo "QA_CASE_EXIT $label $result"
}
configuration true
launch first Alpha
mkdir -p /bench/first-auto
find /bench/states -name '*.auto' -exec cp '{}' /bench/first-auto/ ';'
state=$(find /bench/states -name 'Alpha.state.auto' | head -n 1)
if [ -n "$state" ]; then
    printf 'manual slot 17 sentinel\n' > "${state%.auto}17"
    printf 'manual slot 0 sentinel\n' > "${state%.auto}"
fi
launch second Alpha
find /bench/states -name '*.auto' -exec sha256sum '{}' ';' > /bench/before-off.sha256
configuration false
launch disabled Alpha
find /bench/states -name '*.auto' -exec sha256sum '{}' ';' > /bench/after-off.sha256
configuration true
launch other-content Beta
export QA_OTHER_ID=1
launch other-core Alpha resume_other_libretro.so
unset QA_OTHER_ID
export QA_NO_STATE=1
launch unsupported NoState
unset QA_NO_STATE
echo QA_ALL_DONE
sync
poweroff -f
'''
    if args.direct_core:
        if args.baseline_off:
            init = init[:init.index('configuration true\nlaunch first Alpha')] + '''configuration false
launch first Alpha
launch second Alpha
echo QA_ALL_DONE
sync
poweroff -f
'''
        else:
            init = init[:init.index('export QA_OTHER_ID=1')] + '''echo QA_ALL_DONE
sync
poweroff -f
'''
    if args.regular_tasks:
        init = init.replace('state_slot = 17\n', 'state_slot = 17\nthreaded_data_runloop_enable = false\n')
    if args.capture_crash:
        init = init.replace('cat /proc/meminfo', '''ulimit -c 65536
printf '%s\\n' '/bench/core.%e.%p' > /proc/sys/kernel/core_pattern
cat /proc/sys/kernel/core_pattern > /bench/core-pattern.txt
cat /proc/meminfo''', 1)
    (q / 'bench-init.sh').write_text(init)
    overlay = Newc()
    overlay.file('usr/bin/bench-init', init, 0o755)
    real_ra = B / 'target/usr/bin/retroarch'
    real_core = B / 'target/usr/lib/libretro/fceumm_libretro.so'
    resolver = B / 'target/usr/lib/lilsmoke/lilsmoke-rom'
    overlay.file('usr/bin/retroarch-real', real_ra.read_bytes(), 0o755)
    preload = '/bench/present.so' + (':/bench/mutex-trace.so' if args.trace_mutex else '')
    overlay.file('usr/bin/retroarch', '#!/bin/sh\nexport LD_PRELOAD=' + preload + '\nexec /usr/bin/retroarch-real "$@" --max-frames=30\n', 0o755)
    launcher = P / 'package/lilsmoke-scripts/src/lilsmoke-retroarch'
    reader = launcher.with_name('lilsmoke-config-read')
    for source in (launcher, reader, resolver):
        overlay.file('usr/lib/lilsmoke/' + source.name, source.read_bytes(), 0o755)
    overlay.file('usr/lib/libretro/fceumm-engine.so', real_core.read_bytes(), 0o755)
    info = (B / 'target/usr/share/libretro/info/fceumm_libretro.info').read_bytes()
    for name in ('fceumm', 'resume_other'):
        library = real_core if args.direct_core else q / 'observed.so'
        overlay.file('usr/lib/libretro/' + name + '_libretro.so', library.read_bytes(), 0o755)
        overlay.file('usr/share/libretro/info/' + name + '_libretro.info', info)
    overlay.file('etc/retroarch.cfg', (P / 'package/retroarch-mips/retroarch.cfg').read_bytes())
    overlay.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
    runtime_libraries(B, overlay, [real_ra, real_core, resolver, q / 'observed.so', q / 'present.so'])
    (q / 'boot.cpio.gz').write_bytes(gzip.compress(overlay_initramfs(B, overlay, '/usr/bin/bench-init', overlay_mib=32)))
    command = prefix + ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
        '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120',
        '-kernel', str(B / 'images/vmlinux'), '-initrd', str(q / 'boot.cpio.gz'),
        '-append', 'console=ttyS0 video=cirrusfb:856x480M-16@60 panic=-1',
        '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
        '-no-reboot', '-device', 'usb-kbd', *disk_devices(B, [('bench', q, False)])]
    commands.append(command)
    (q / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    print('Running installed RetroArch/FCEUmm in one-CPU 120 MiB MIPS guest: ' + str(q), flush=True)
    with (q / 'serial.log').open('wb') as log:
        vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        halted = False
        try:
            deadline = time.monotonic() + args.timeout
            while vm.poll() is None:
                trace = (q / 'serial.log').read_text(errors='replace')
                if 'QA_ALL_DONE' in trace and 'System halted' in trace:
                    halted = True
                    break
                if time.monotonic() > deadline:
                    raise TimeoutError('MIPS auto-resume runtime exceeded bound')
                time.sleep(.2)
        finally:
            if vm.poll() is None:
                vm.terminate()
                try:
                    vm.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    vm.kill()
                    vm.wait()
    assert halted or vm.returncode == 0, vm.returncode
    serial = (q / 'serial.log').read_text(errors='replace')
    assert 'QA_ALL_DONE' in serial, serial[-3000:]
    names = (('first', 'second') if args.baseline_off else
             ('first', 'second', 'disabled', 'other-content') if args.direct_core else
             ('first', 'second', 'disabled', 'other-content', 'other-core', 'unsupported'))
    logs = {name: (q / (name + '.log')).read_text(errors='replace') for name in names}
    exits = {name: int((q / (name + '.exit')).read_text().strip()) for name in names}
    (q / 'exits.json').write_text(json.dumps(exits, indent=2) + '\n')
    if args.capture_crash:
        # Symbol files and SDK remain read-only. Core payloads stay private in q.
        for core_dump in sorted(q.glob('core.*')):
            debugger = ['/usr/bin/gdb-multiarch', '-batch', '-nx',
                '-ex', 'set pagination off', '-ex', 'set print frame-arguments all',
                '-ex', 'set sysroot ' + str(B / 'target'),
                '-ex', 'set solib-search-path ' + str(q) + ':' + str(B / 'target/usr/lib'),
                '-ex', 'info files', '-ex', 'info sharedlibrary',
                '-ex', 'thread apply all bt', '-ex', 'info registers',
                '-ex', 'x/12i $pc-16', '-ex', 'x/48wx $sp',
                str(B / 'build/retroarch-mips-v1.19.1/retroarch'), str(core_dump)]
            commands.append(debugger)
            with (q / (core_dump.name + '.gdb.txt')).open('w') as log:
                subprocess.run(prefix + debugger, stdout=log, stderr=subprocess.STDOUT,
                               timeout=45, check=False)
        (q / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    memory = (q / 'memory.txt').read_text()
    mem_kib = int(re.search(r'MemTotal:\s+(\d+)', memory)[1])
    assert mem_kib <= 120 * 1024
    assert re.search(r'SwapTotal:\s+0\s+kB', memory)
    assert len((q / 'swaps.txt').read_text().splitlines()) == 1
    assert len(re.findall(r'^processor\s*:', (q / 'cpu.txt').read_text(), re.M)) == 1
    if args.direct_core:
        # No emulator API interposition in this branch: verify the frontend's
        # real load/save logs, file isolation and clean process status directly.
        report = dict(passed=all(code == 0 for code in exits.values()), exits=exits,
                      direct_installed_core=True, baseline_option_off=args.baseline_off,
                      installed_retroarch_sha256=digest(real_ra), installed_core_sha256=digest(real_core),
                      cpus=1, ram_mib=120, memtotal_kib=mem_kib, swap_kib=0,
                      regular_tasks_qa_override=args.regular_tasks,
                      card_writes=False, actual_audio_video_tested=False, hashes=inputs)
        if not args.baseline_off:
            report['first_auto_saved'] = 'Auto save state to ' in logs['first'] and ' succeeded.' in logs['first']
            report['second_auto_loaded'] = '[State]: Loading state ' in logs['second']
            report['disabled_did_not_load_or_save'] = '[State]: Loading state ' not in logs['disabled'] and 'Auto save state to ' not in logs['disabled']
            report['other_content_did_not_load'] = '[State]: Loading state ' not in logs['other-content']
            report['auto_state_unchanged_when_disabled'] = (q / 'before-off.sha256').read_bytes() == (q / 'after-off.sha256').read_bytes()
            report['passed'] &= all(report[key] for key in ('first_auto_saved', 'second_auto_loaded',
                'disabled_did_not_load_or_save', 'other_content_did_not_load', 'auto_state_unchanged_when_disabled'))
        (q / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        assert report['passed'], 'Direct-core lifecycle failed; see report/exits/raw logs'
        return
    for name in names:
        assert (q / (name + '.exit')).read_text().strip() == '0', name
        assert 'QA_LOAD ok=1' in logs[name], name
        assert 'QA_UNLOAD frames=30' in logs[name], (name, logs[name][-1500:])
        cfg = (q / (name + '.cfg')).read_text()
        enabled = 'false' if name == 'disabled' else 'true'
        assert re.search(r'^savestate_auto_save = ' + enabled + '$', cfg, re.M), name
        assert re.search(r'^savestate_auto_load = ' + enabled + '$', cfg, re.M), name
        assert re.search(r'^state_slot\s*=\s*17$', cfg, re.M), name
    saved = re.findall(r'QA_SERIALIZE ok=1 hash=([0-9a-f]+)', logs['first'])
    restored = re.findall(r'QA_UNSERIALIZE ok=1 hash=([0-9a-f]+) bytes=\d+ frames=(\d+)', logs['second'])
    assert saved and len(restored) == 1 and saved[-1] == restored[0][0], 'RAM roundtrip differs'
    # RetroArch queues the load; this build runs one initial frame first. Its
    # undo-state backup also calls serialize, so inspect the LAST exit save.
    assert 0 <= int(restored[0][1]) < 30, 'load did not happen within content execution'
    advanced = re.findall(r'QA_SERIALIZE ok=1 hash=([0-9a-f]+)', logs['second'])
    assert advanced and advanced[-1] != restored[0][0], 'RAM must advance after resume'
    for name in ('first', 'disabled', 'other-content', 'other-core', 'unsupported'):
        assert 'QA_UNSERIALIZE' not in logs[name], name
    for name in ('disabled', 'unsupported'):
        assert 'QA_SERIALIZE' not in logs[name], name
    assert (q / 'before-off.sha256').read_bytes() == (q / 'after-off.sha256').read_bytes()
    autos = sorted(str(f.relative_to(q)) for f in (q / 'states').rglob('*.auto'))
    assert len(autos) == 3 and all((q / path).stat().st_size > 0 for path in autos), autos
    alpha_paths = [f for f in (q / 'states').rglob('Alpha.state.auto')]
    assert len(alpha_paths) == 2 and alpha_paths[0].parent != alpha_paths[1].parent, autos
    manual = list((q / 'states').rglob('Alpha.state17')) + list((q / 'states').rglob('Alpha.state'))
    assert len(manual) == 2, manual
    for f in manual:
        slot = 17 if f.name.endswith('17') else 0
        assert f.read_text() == f'manual slot {slot} sentinel\n', str(f)
    report = dict(passed=True, cases=list(names), actual_installed_retroarch=True,
                  actual_fceumm_with_observer=True, core_identity_probe_not_second_emulator=True,
                  guest='Linux/MIPS32 74Kf profile, musl', cpus=1, ram_mib=120,
                  memtotal_kib=mem_kib, swap_kib=0, frames_per_launch=30,
                  saved_ram_hash=saved[-1], restored_ram_hash=restored[0][0],
                  resume_after_initial_frames=int(restored[0][1]),
                  next_exit_ram_hash=advanced[-1], manual_slots_preserved=[0, 17],
                  auto_states=autos, disabled_leaves_state_unchanged=True,
                  unsupported_serialize_size_zero_exits_cleanly=True,
                  physical_console_tested=False, real_audio_video_tested=False,
                  regular_tasks_qa_override=args.regular_tasks,
                  card_writes=False, sdk_modified=False,
                  hashes=inputs)
    (q / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
