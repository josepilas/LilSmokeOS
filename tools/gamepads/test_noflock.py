#!/usr/bin/env python3
"""Serial, isolated no-advisory-lock acceptance. Never installs into SDK/card."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import signal
import struct
import subprocess
import time

P = Path(__file__).resolve().parents[2]
T = P / 'tools/gamepads'
SRC = P / 'package/lilsmoke-usb-gamepads/src'
COMMON = P / 'package/lilsmoke-ui/src/src'
B = Path('/root/lilsmoke-os/build/buildroot/output')


def run(command, **kwargs):
    print('+', shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, timeout=90, **kwargs)


def wait_for(predicate, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(.01)
    raise AssertionError('timed out waiting for acceptance condition')


def empty_lock(state):
    root = Path(str(state) + '.lock.d')
    assert root.is_dir() and not list(root.iterdir()), list(root.iterdir())


def race(command, env, folder, peers=8, rounds=4):
    folder.mkdir(exist_ok=True)
    state = folder / 'state'
    for turn in range(rounds):
        gate_in, gate_out = os.pipe()
        processes, logs = [], []
        try:
            for i in range(peers):
                log = (folder / f'{turn}-{i}.log').open('w+')
                logs.append(log)
                processes.append(subprocess.Popen(
                    [*map(str, command), 'hold', str(state)],
                    env=dict(env, LOCK_QA_GATE=str(gate_in)), pass_fds=(gate_in,),
                    stdin=subprocess.PIPE, stdout=log, stderr=log))
            os.close(gate_in)
            gate_in = -1
            os.write(gate_out, b'x' * peers)
            os.close(gate_out)
            gate_out = -1

            def settled():
                return sum(p.poll() is None for p in processes) == 1 and any(
                    'ACQUIRED' in Path(log.name).read_text() for log in logs)

            wait_for(settled)
            winners = [p for p in processes if p.poll() is None]
            assert len(winners) == 1
            assert sum('ACQUIRED' in Path(log.name).read_text() for log in logs) == 1
            for proc in processes:
                if proc is not winners[0]:
                    assert proc.returncode == 2
            # An established owner must keep excluding later startups.
            other = subprocess.run([*map(str, command), 'hold', str(state)], env=env,
                                   input=b'x', capture_output=True, timeout=12)
            assert other.returncode == 2 and b'BUSY' in other.stderr, other
            if turn % 2 == 0:
                winners[0].send_signal(signal.SIGKILL)
                winners[0].wait(timeout=12)
                assert list(Path(str(state) + '.lock.d').iterdir())
            else:
                winners[0].communicate(b'x', timeout=12)
                assert winners[0].returncode == 0
                empty_lock(state)
        finally:
            for fd in (gate_in, gate_out):
                if fd >= 0:
                    os.close(fd)
            for proc in processes:
                if proc.poll() is None:
                    proc.kill()
                proc.wait(timeout=12)
                if proc.stdin:
                    proc.stdin.close()
            for log in logs:
                log.close()
    empty_lock(state)


def snapshot(state):
    try:
        a, b = state.read_bytes(), state.read_bytes()
    except FileNotFoundError:
        return None
    if len(a) != 424 or a != b:
        return None
    magic, abi, seq, pid, stamp = struct.unpack_from('<IIIIQ', a)
    if seq % 2:
        return None
    assert magic == 0x4c534750 and abi == 1 and pid
    connected, exported, instance, buttons = struct.unpack_from('<IIII', a, 24)
    return a, (connected, exported, instance, buttons), stamp, pid


def daemon(command, env, folder, fixture):
    folder.mkdir(exist_ok=True)
    state = folder / 'state'
    env = dict(env, LILSMOKE_GAMEPAD_STATE=str(state), SDL_VIDEODRIVER='dummy',
               LILSMOKE_GAMEPADS_SHARED_ONLY='1', LILSMOKE_GAMEPAD_FIXTURE_HOTPLUG='1')
    for generation in range(2):
        with (folder / f'run-{generation}.log').open('w') as log:
            old_inode = state.stat().st_ino if state.exists() else None
            proc = subprocess.Popen([*map(str, command), fixture], env=env, stdout=log, stderr=log)
            try:
                seen, instances, stamps = [], [], []
                deadline = time.monotonic() + 15
                duplicate_done = False
                while time.monotonic() < deadline and proc.poll() is None:
                    s = snapshot(state)
                    if s and (old_inode is None or state.stat().st_ino != old_inode):
                        raw, pad, stamp, pid = s
                        connected, exported, instance, buttons = pad
                        assert exported == 0
                        if connected:
                            axes = struct.unpack_from('<6h', raw, 40)
                            assert buttons & (1 << 2) and axes[0] > 15000
                            if fixture == '--fixture':
                                assert axes[3] == -32768 and axes[4] == 32767, axes
                            if not seen or seen[-1] != 'connected':
                                seen.append('connected')
                                instances.append(instance)
                            stamps.append(stamp)
                            if not duplicate_done:
                                inode = state.stat().st_ino
                                other = subprocess.run([*map(str, command), fixture], env=env,
                                                       capture_output=True, timeout=12)
                                assert other.returncode == 1 and b'state_owner_busy' in other.stderr, other
                                assert state.stat().st_ino == inode and snapshot(state)[3] == pid
                                duplicate_done = True
                        elif seen and seen[-1] != 'disconnected':
                            assert not any(raw[24:]), 'disconnect did not neutralize all slots'
                            seen.append('disconnected')
                    time.sleep(.008)
                proc.wait(timeout=12)
                assert proc.returncode == 0, Path(log.name).read_text()
                assert seen[:3] == ['connected', 'disconnected', 'connected'], seen
                assert instances[0] != instances[1], instances
                assert stamps[-1] - stamps[0] >= 1000, stamps
                assert not any(state.read_bytes()[24:])
                empty_lock(state)
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.wait(timeout=12)
    # SIGKILL leaves a stale claim and old mmap generation. New daemon reaps it
    # and publishes a distinct inode; clients retain the unchanged 424-byte ABI.
    with (folder / 'crash.log').open('w') as log:
        proc = subprocess.Popen([*map(str, command), fixture], env=env, stdout=log, stderr=log)
        try:
            wait_for(lambda: (snapshot(state) or (None, [0]))[1][0])
            old_inode = state.stat().st_ino
            proc.kill()
            proc.wait(timeout=12)
            proc = subprocess.Popen([*map(str, command), fixture], env=env, stdout=log, stderr=log)
            wait_for(lambda: state.stat().st_ino != old_inode and bool((snapshot(state) or (None, [0]))[1][0]))
            proc.terminate()
            proc.wait(timeout=12)
            assert proc.returncode == 0 and not any(state.read_bytes()[24:])
            empty_lock(state)
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait(timeout=12)


def diagnostics(output):
    folder = output / 'diagnostics'
    folder.mkdir(exist_ok=True)
    helper = folder / 'session-log'
    helper.write_text('#!/bin/sh\nprintf "%s\\n" "$LOG_QA_PATH"\n')
    helper.chmod(0o700)
    script = (SRC / 'lilsmoke-input-diagnostics').read_text()
    assert script.count('/usr/lib/lilsmoke/lilsmoke-session-log') == 1
    script = script.replace('/usr/lib/lilsmoke/lilsmoke-session-log', str(helper))
    script = script.replace('/persist/logs/input-hardware.log', '/proc/lilsmoke-qa-readonly/input.log')
    test = folder / 'diagnostics.sh'
    test.write_text(script)
    bindir = folder / 'bin'
    bindir.mkdir(exist_ok=True)
    dmesg = bindir / 'dmesg'
    dmesg.write_text('#!/bin/sh\nprintf "%s\\n" "USB test" "FAT-fs: error" "mmc0: error" '
                    '"Remounting filesystem read-only" "Buffer I/O error" "Unrelated marker"\n')
    dmesg.chmod(0o700)
    fallback = folder / 'fallback.log'
    env = dict(os.environ, PATH=str(bindir) + ':' + os.environ['PATH'], LOG_QA_PATH=str(fallback))
    run(['sh', test], env=env, stdout=subprocess.DEVNULL)
    data = fallback.read_text()
    for marker in ('USB test', 'FAT-fs: error', 'mmc0: error', 'read-only', 'Buffer I/O error'):
        assert marker in data
    assert 'Unrelated marker' not in data
    explicit = folder / 'explicit.log'
    run(['sh', test, explicit], env=env, stdout=subprocess.DEVNULL)
    assert 'Kernel=' in explicit.read_text()
    run(['sh', test], env=dict(env, LOG_QA_PATH='/proc/lilsmoke-qa-readonly/missing'), stdout=subprocess.DEVNULL)
    helper.unlink()
    run(['sh', test], env=env, stdout=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('/root/usb-gamepads-noflock-20261018'))
    parser.add_argument('--mode', action='append', choices=('host', 'mips'))
    args = parser.parse_args()
    q = args.output
    q.mkdir(parents=True, exist_ok=True)
    sdk = B / 'staging'
    s07 = SRC.parent / 'S07usb-gamepads'
    s07_hash = hashlib.sha256(s07.read_bytes()).hexdigest()
    results = []
    for mode in (args.mode or ('host', 'mips')):
        cc = 'gcc' if mode == 'host' else str(B / 'host/bin/mipsel-buildroot-linux-musl-gcc')
        flags = ['-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-I' + str(SRC), '-I' + str(COMMON)]
        if mode == 'mips':
            flags += ['-march=mips32r2', '-mtune=74kc', '-mdspr2', '-mfp32', '-mhard-float']
        owner = q / ('owner-' + mode)
        shim = q / ('deny-' + mode + '.so')
        binary = q / ('daemon-' + mode)
        run([cc, *flags, T / 'test_owner_lock.c', '-o', owner])
        run([cc, *flags, '-shared', '-fPIC', T / 'deny_advisory_locks.c', '-ldl', '-o', shim])
        sdl = ['-I' + str(sdk / 'usr/include/SDL2')] if mode == 'mips' else shlex.split(
            subprocess.check_output(['pkg-config', '--cflags', 'sdl2'], text=True))
        run([cc, *flags, *sdl, SRC / 'usb-gamepads.c', '-lSDL2', '-o', binary])
        # QEMU -L <sysroot> rewrites open("/") to the SDK root, unlike M8.
        # Invoke its musl loader explicitly so anchored openat uses real paths.
        prefix = [] if mode == 'host' else ['qemu-mipsel', '-cpu', '74Kf', '-L', '/']
        loader = [] if mode == 'host' else [str(sdk / 'lib/ld-musl-mipsel.so.1'),
                    '--library-path', str(sdk / 'lib') + ':' + str(sdk / 'usr/lib')]
        for denied in (False, True):
            label = mode + ('-enosys' if denied else '-normal')
            env = dict(os.environ)
            pre = prefix.copy()
            if denied:
                if mode == 'mips':
                    pre += ['-E', 'LD_PRELOAD=' + str(shim)]
                else:
                    env['LD_PRELOAD'] = str(shim)
            pre += loader
            unit = q / ('unit-' + label)
            unit.mkdir(exist_ok=True)
            run([*pre, owner, 'unit', unit / 'state'], env=env)
            results.append({'case': 'owner-safety-unit', 'mode': label, 'passed': True})
            race([*pre, owner], env, q / ('race-' + label))
            print('PASS concurrent startup/crash/cleanup:', label, flush=True)
            results.append({'case': '32-concurrent-starts-crash-reclaim-cleanup', 'mode': label, 'passed': True})
            for fixture in ('--fixture', '--rk707-fixture'):
                daemon([*pre, binary], env, q / (label + fixture), fixture)
                print('PASS daemon hotplug/restart:', label, fixture, flush=True)
                results.append({'case': fixture + '-hotplug-restart-abi424-single-owner', 'mode': label, 'passed': True})
        # Existing unchanged raw PS decoder, raw hotplug, and frozen legacy ABI.
        for name, sources, extra in (
            ('rk707', [T / 'test_rk707.c'], []),
            ('raw-hotplug', [T / 'test_daemon_hotplug.c'], sdl + ['-lSDL2']),
            ('legacy-state', [T / 'test_state.c', T / 'test_legacy_reader.c'],
             ['-Wl,--wrap=clock_gettime'] + (['-Wl,--wrap=__clock_gettime64'] if mode == 'mips' else []))):
            test = q / (name + '-' + mode)
            run([cc, *flags, *sources, *extra, '-o', test])
            run([*prefix, *loader, test])
            results.append({'case': name, 'mode': mode, 'passed': True})
    diagnostics(q)
    results.append({'case': 'diagnostics-readonly-fallback-explicit-devnull-storage-filter', 'mode': 'host-shell', 'passed': True})
    artifact = q / 'lilsmoke-usb-gamepads'
    run([B / 'host/bin/mipsel-buildroot-linux-musl-strip', '-o', artifact, q / 'daemon-mips'])
    assert hashlib.sha256(s07.read_bytes()).hexdigest() == s07_hash, 'parent S07 changed during acceptance'
    report = dict(passed=True, cases=results, abi_bytes=424, abi_version=1,
                  artifact=str(artifact), sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),
                  s07_sha256=s07_hash, sdk_writes=False, physical_usb=False, card_writes=False)
    report['modes'] = args.mode or ['host', 'mips']
    (q / 'acceptance.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
