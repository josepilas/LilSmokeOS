#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Real shared-transport/dlopen/signal cleanup tests, without a full-system VM."""
import argparse
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import threading
import time

COMBO = (1 << 4) | (1 << 6)


def test(build, runner, show_results=True):
    profile = json.loads((build / 'profile.json').read_text())
    results = []
    with tempfile.TemporaryDirectory(prefix='lilretro-hotkey-') as temporary:
        root = Path(temporary)

        def case(name, action, mode=9, held=False):
            work = root / name
            work.mkdir()
            config = dict(profile, controller_port=1)
            (work / 'profile.json').write_text(json.dumps(config))
            pads = [[0, 0, 0] for _ in range(4)]
            pads[3] = [1, 10, COMBO if held else 0]
            lock = threading.Lock()
            ended = threading.Event()
            path = work / 'pads'
            fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
            os.ftruncate(fd, 424)
            sequence = 0

            def publish():
                nonlocal sequence
                sequence += 2
                packet = struct.pack('<IIIIQ', 0x4c534750, 1, sequence | 1, os.getpid(),
                                     int(time.monotonic() * 1000))
                for connected, instance, buttons in pads:
                    packet += struct.pack('<IIII6hHHHH64s', connected, 0, instance, buttons,
                                          *([0] * 10), b'hotkey-test')
                assert len(packet) == 424
                os.pwrite(fd, struct.pack('<I', sequence | 1), 8)
                os.pwrite(fd, packet, 0)
                os.pwrite(fd, struct.pack('<I', sequence), 8)

            def update():
                while not ended.is_set():
                    with lock:
                        publish()
                    ended.wait(0.008)

            def buttons(value):
                with lock:
                    pads[3][2] = value

            def replace_transport():
                nonlocal fd
                with lock:
                    replacement = work / 'replacement'
                    new = os.open(replacement, os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
                    os.ftruncate(new, 424)
                    os.close(fd)
                    fd = new
                    publish()
                    os.replace(replacement, path)

            publish()
            thread = threading.Thread(target=update)
            thread.start()
            env = dict(os.environ, LILSMOKE_GAMEPAD_STATE=str(path), TEST_CORE_MODE=str(mode),
                       TEST_CORE_LIFECYCLE=str(work / 'lifecycle'),
                       SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
            env.pop('TF_DRIVER', None)
            env.pop('TF_PRESENT', None)
            command = [*runner, str(build / 'lilretro'), str(build / 'test_core.so'),
                       '--profile', str(work / 'profile.json'), '--headless',
                       '--diagnostics', str(work / 'report.json')]
            process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 10
                while not (work / 'lifecycle').exists() or 'run\n' not in (work / 'lifecycle').read_text():
                    assert process.poll() is None, process.communicate()
                    assert time.monotonic() < deadline, 'core never entered retro_run'
                    time.sleep(0.005)

                def alive(seconds):
                    time.sleep(seconds)
                    assert process.poll() is None, (name, 'unexpected exit', process.communicate())

                action(process, buttons, alive, replace_transport)
                stdout, stderr = process.communicate(timeout=3)
                assert process.returncode == 0, (name, process.returncode, stdout, stderr)
                report = json.loads((work / 'report.json').read_text())
                assert report['ok'] and report['observed']['frames'] > 0, report
                assert (work / 'lifecycle').read_text().splitlines() == ['load', 'run', 'unload', 'deinit']
                assert (work / 'saves/sram.bin').stat().st_size == 16
                assert (work / 'saves/sram.bin').read_bytes()[0] != 0
                assert not list(work.rglob('*.tmp.*'))
                if mode == 10:
                    assert report['observed']['frames'] == 1, report
                results.append(dict(test=name, passed=True, frames=report['observed']['frames']))
            finally:
                ended.set()
                thread.join()
                os.close(fd)
                if process.poll() is None:
                    process.kill()
                    process.communicate()

        def compound(process, buttons, alive, replace):
            alive(0.15)
            buttons(1 << 6)
            alive(0.1)
            buttons(COMBO | (1 << 2))
            alive(0.3)
            buttons(1 << 6)
            alive(0.04)
            buttons(COMBO)
            alive(0.6)
            buttons(0)
            alive(0.14)
            buttons(COMBO | (1 << 2))

        def launch_held(process, buttons, alive, replace):
            alive(0.7)
            buttons(0)
            alive(0.14)
            buttons(COMBO)

        def replaced(process, buttons, alive, replace):
            alive(0.15)
            buttons(COMBO)
            alive(0.3)
            replace()
            alive(0.7)
            buttons(0)
            alive(0.14)
            buttons(COMBO)

        def slow_core(process, buttons, alive, replace):
            alive(0.14)
            buttons(COMBO)

        case('core-never-polls-slot4-compound-release', compound)
        case('launch-held-requires-release', launch_held, held=True)
        case('atomic-transport-replacement-held', replaced)
        case('host-polls-during-one-second-frame-wait', slow_core, mode=10)
        for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            def send(process, buttons, alive, replace, sig=sig):
                alive(0.1)
                # Signal the Linux runtime, never inject Windows keyboard input.
                if runner:
                    raise AssertionError('signal cases require a native Linux process')
                process.send_signal(sig)
            if not runner:
                case('graceful-' + sig.name, send)
    if show_results:
        print(json.dumps(results, indent=2))
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--runner', nargs='*', default=[])
    parser.add_argument('--runner-json')
    parser.add_argument('--repeat', type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.repeat <= 100:
        parser.error('--repeat must be between 1 and 100')
    runner = json.loads(args.runner_json) if args.runner_json else args.runner
    for repetition in range(args.repeat):
        results = test(args.build.resolve(), runner, show_results=args.repeat == 1)
        if args.repeat > 1:
            print(f'HOTKEY_SHARED_REPEAT run={repetition + 1}/{args.repeat} passed={len(results)}', flush=True)
