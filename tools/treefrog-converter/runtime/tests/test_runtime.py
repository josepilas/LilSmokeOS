#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Bounded real dlopen/API tests, no game data or hardware claims."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time

HERE = Path(__file__).resolve().parent.parent

def test(args):
    if args.runner_json:
        args.runner = json.loads(args.runner_json)
    out = args.build.resolve()
    profile = json.loads((out / 'profile.json').read_text())
    results = []
    def run(name, mode=0, extra=(), ok=True, patch=None, headless=True, state=None, board=False):
        work = out / 'tests' / name
        work.mkdir(parents=True, exist_ok=True)
        p = dict(profile)
        if patch:
            p.update(patch)
        (work / 'profile.json').write_text(json.dumps(p))
        env = dict(os.environ, TEST_CORE_MODE=str(mode), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   LILSMOKE_GAMEPAD_STATE=str(work / 'pads'))
        env.pop('TF_DRIVER',None)
        env.pop('TF_PRESENT',None)
        if board:
            env.update(TF_DRIVER=str(out/'test_driver.so'),TF_PRESENT='dispframe',TEST_DRIVER_REPORT=str(work/'driver.json'))
        if state is not None:
            transport = tempfile.NamedTemporaryFile(prefix='lilretro-input-', dir='/dev/shm')
            env['LILSMOKE_GAMEPAD_STATE'] = transport.name
            pad = struct.pack('<IIII6hHHHH64s', 1, 0, 1, 1, 0, 0, 12345, 0, 0, 0, 0, 0, 0, 0, b'test')
            active = threading.Event()
            def update():
                while not active.is_set():
                    stamp = int(time.monotonic() * 1000) - (2000 if state == 'stale' else 0)
                    packet = struct.pack('<IIIIQ', 0x4c534750, 1, 2, os.getpid(), stamp) + pad + bytes(300)
                    assert len(packet) == 424
                    os.pwrite(transport.fileno(), packet, 0)
                    active.wait(0.01)
            update_thread = threading.Thread(target=update)
            update_thread.start()
        command = [*args.runner, str(out / 'lilretro'), str(out / 'test_core.so'), '--profile', str(work / 'profile.json'),
                   '--probe', '--frames', '6', '--diagnostics', str(work / 'report.json')]
        if headless:
            command.append('--headless')
        command += extra
        done = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        if state is not None:
            active.set()
            update_thread.join()
            transport.close()
        assert (work / 'report.json').exists(), (name, done.returncode, done.stderr.decode())
        report = json.loads((work / 'report.json').read_text())
        assert (done.returncode == 0) == ok, (name, done.returncode, done.stderr.decode(), report)
        assert report['ok'] == ok
        results.append(dict(test=name, passed=True, exit=done.returncode))
        return report, work
    r, _ = run('rgb565')
    assert r['observed']['video_frames'] == 4 and r['observed']['duplicates'] == 2
    assert r['observed']['audio_frames'] == 4410 and r['observed']['nonzero_audio_samples'] > 0
    assert r['options'][0]['value'] == 'red' and r['options'][0]['choices'] == ['red', 'green']
    assert any(e['command'] == 14 and e['accepted'] == 0 for e in r['environment'])
    assert r['capabilities']['hardware_render'] is False and r['capabilities']['vfs'] is False
    run('xrgb8888', mode=1)
    run('video-budget', mode=2, ok=False)
    run('audio-budget', mode=3, ok=False)
    r, _ = run('software-framebuffer', mode=4)
    assert r['observed']['software_framebuffer_requests'] == 6
    run('options', patch={'options': {'test_color': 'green'}})
    run('options-invalid', patch={'options': {'test_color': 'blue'}}, ok=False)
    run('wrong-abi', patch={'abi': 'not-this-libc'}, ok=False)
    run('invalid-budget', patch={'max_width': 10000}, ok=False)
    run('input-digital-analog', mode=5, state='fresh')
    run('input-stale-clears', mode=6, state='stale')
    r, work = run('state-save', extra=['--save-state'])
    assert (work / 'saves/state.bin').stat().st_size == 4
    run('state-save', extra=['--load-state'])
    run('state-unsupported', mode=7, extra=['--save-state'], ok=False)
    run('state-core-refuses', mode=8, extra=['--save-state'], ok=False)
    if args.skip_sdl_device:
        results.append(dict(test='sdl-software-pcm', skipped=True, reason='Target SDK has no dummy SDL driver; device/full-VM test required'))
    else:
        run('sdl-software-pcm', headless=False)
    if 'musl' not in profile['abi']:
        for mode in (0,1):
            r, work=run('board-dispframe-'+str(mode),mode=mode,headless=False,board=True)
            d=json.loads((work/'driver.json').read_text())
            assert d['frames']==4 and d['pcm_frames']==4410 and d['energy']>0 and d['rate']==44100
            assert r['sinks']['video']=='dispframe' and r['sinks']['audio']=='board-pcm'
    else:
        run('reject-glibc-board-ABI',headless=False,board=True,ok=False)
    (out / 'test-results.json').write_text(json.dumps(dict(passed=True, tests=results), indent=2) + '\n')
    print(json.dumps(results, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=HERE / '_build/host')
    parser.add_argument('--runner', nargs='*', default=[])
    parser.add_argument('--runner-json')
    parser.add_argument('--skip-sdl-device', action='store_true')
    test(parser.parse_args())
