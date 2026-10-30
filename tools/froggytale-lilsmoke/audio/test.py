#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Headless audio checks; owner PCM is emitted ONLY to an explicit private folder."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import wave

HERE = Path(__file__).resolve().parent


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=('host', 'mips'), default='mips')
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--wad', type=Path)
    parser.add_argument('--ogg', type=Path)
    parser.add_argument('--private-out', type=Path)
    parser.add_argument('--no-capture', action='store_true', help='Verify owner PCM in memory only')
    args = parser.parse_args()
    if (args.wad or args.ogg) and not args.private_out:
        parser.error('Owner tests require --private-out (never packaged)')
    if not args.no_build:
        subprocess.run([sys.executable, '-B', str(HERE / 'build.py'), '--target', args.target], check=True)
    binary = HERE / '_build' / args.target / 'audio-probe'
    sdk = Path(os.environ.get('SDK', '/root/lilsmoke-os/build/buildroot/output'))
    out = HERE / '_build' / args.target / 'tests'
    out.mkdir(parents=True, exist_ok=True)
    prefix = ['taskset', '-c', os.environ.get('FROGGY_AUDIO_CPU', '3'), 'nice', '-n', '15',
              '/usr/bin/time', '-f', 'AUDIO_RESOURCES elapsed=%e rss_kib=%M']
    if args.target == 'mips':
        prefix += ['qemu-mipsel', '-cpu', '74Kf', '-L', str(sdk / 'staging')]
    prefix += [str(binary)]
    env = os.environ.copy()
    for key in ('LD_PRELOAD', 'LILSMOKE_HCFB', 'LILSMOKE_M8_AUDIO', 'LILSMOKE_OD_SDL'):
        env.pop(key, None)
    env.update(SDL_AUDIODRIVER='dummy', FROGGY_AUDIO_MANUAL='1')
    report = dict(target=args.target, binary_sha256=digest(binary), physical_audio_tested=False,
                  passed=False, results=[])
    report_path = out / 'report.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n')

    def run(name, options, directory=out, overrides=None):
        start = time.monotonic()
        with (directory / (name + '.log')).open('w') as log:
            proc = subprocess.run(prefix + list(map(str, options)), env=env | (overrides or {}),
                                  stdout=log, stderr=subprocess.STDOUT, timeout=600)
        text = (directory / (name + '.log')).read_text(errors='replace')
        result = dict(name=name, exit_code=proc.returncode, elapsed_seconds=time.monotonic() - start,
                      proof_lines=[line for line in text.splitlines() if line.startswith('AUDIO_')])
        report['results'].append(result)
        report_path.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(result), flush=True)
        if proc.returncode or 'PASS' not in text:
            print(text[-8000:], flush=True)
            raise RuntimeError('Audio check failed: ' + name)

    run('synthetic-apis', [])
    run('device-failure', ['--expect-device-failure'], overrides={'SDL_AUDIODRIVER': 'froggy-nonexistent-driver'})
    synthetic = out / 'generated-tone.wav'
    with wave.open(str(synthetic), 'wb') as audio:
        audio.setparams((1, 2, 32000, 0, 'NONE', 'not compressed'))
        audio.writeframes(b''.join(struct.pack('<h', int(10000 * math.sin(2 * math.pi * 330 * i / 32000)))
                                   for i in range(32000)))
    run('automatic-sdl-pull', ['--ogg', synthetic, '--automatic'])
    if args.wad or args.ogg:
        args.private_out.mkdir(parents=True, exist_ok=False)
        inputs = {str(path): digest(path) for path in (args.wad, args.ogg) if path}
        options = []
        if args.wad:
            options += ['--wad', args.wad.resolve(strict=True)]
        if args.ogg:
            options += ['--ogg', args.ogg.resolve(strict=True)]
        capture = args.private_out / 'OWNER-DO-NOT-PACKAGE.s16le'
        try:
            run('owner-pcm', options + ([] if args.no_capture else ['--capture-private', capture]), args.private_out)
            if args.ogg:
                run('owner-sdl-callback', ['--ogg', args.ogg.resolve(), '--automatic'], args.private_out)
            report['owner_inputs_unchanged'] = all(digest(Path(path)) == value for path, value in inputs.items())
            if not report['owner_inputs_unchanged']:
                raise RuntimeError('Owner input changed')
            report['owner_pcm_tested'] = True
            report['owner_capture_retained'] = not args.no_capture
            if not args.no_capture:
                data = capture.read_bytes()
                pcm = struct.unpack('<' + 'h' * (len(data) // 2), data)
                report['private_capture'] = dict(bytes=len(data), sample_rate=44100, channels=2,
                                                 sha256=hashlib.sha256(data).hexdigest(),
                                                 nonzero_samples=sum(value != 0 for value in pcm),
                                                 peak=max(map(abs, pcm), default=0))
                assert report['private_capture']['nonzero_samples'] > 100
            report['passed'] = True
        finally:
            (args.private_out / 'PRIVATE-report.json').write_text(json.dumps(report, indent=2) + '\n')
    report['passed'] = True
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print('AUDIO_TESTS PASS', flush=True)


if __name__ == '__main__':
    main()
