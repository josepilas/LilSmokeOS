#!/usr/bin/env python3
"""Bounded history, real session wait/exec and production Recents view regression."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import subprocess

P = Path(__file__).resolve().parents[2]
S = P / 'package/lilsmoke-ui/src/src'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    files = sorted((P / 'package/lilsmoke-ui').rglob('*.cpp')) + sorted((P / 'package/lilsmoke-ui').rglob('*.h'))
    files += [Path(__file__), P / 'tools/ui/recent-games-test.cpp']
    def hashes():
        return {str(p.relative_to(P)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    report = dict(passed=False, inputs_before=hashes(), commands=[])
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    def limit():
        resource.setrlimit(resource.RLIMIT_AS, (768 * 1024**2, 768 * 1024**2))
    def run(name, command, compiler=False):
        report['commands'].append(dict(name=name, argv=list(map(str, command))))
        with (args.out / (name + '.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env,
                                    preexec_fn=limit if compiler else None, timeout=600)
        print(name, result.returncode, flush=True)
        if result.returncode:
            print((args.out / (name + '.log')).read_text()[-6000:])
        result.check_returncode()
    try:
        for arch in ('host', 'mips'):
            cxx = 'g++' if arch == 'host' else str(args.sdk / 'host/bin/mipsel-buildroot-linux-musl-g++')
            flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                     '-ffunction-sections', '-fdata-sections', '-I' + str(S),
                     '-I' + str(P / 'package/lilsmoke-scripts/src'), '-I' + str(P / 'package/lilsmoke-media/src')]
            if arch == 'host':
                flags += shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl2', 'SDL2_image', 'SDL2_ttf'], text=True))
                flags += ['-fsanitize=address,undefined', '-fno-pie', '-fno-omit-frame-pointer']
                link = ['-fsanitize=address,undefined', '-no-pie']
                prefix = []
            else:
                flags += ['-I' + str(args.sdk / 'staging/usr/include'), '-include', str(P / 'package/lilsmoke-od-sdl/src/input-wire.h')]
                link = []
                prefix = ['qemu-mipsel', '-cpu', '74Kf', '-L', str(args.sdk / 'target')]
            objects = []
            for source in [P / 'tools/ui/recent-games-test.cpp'] + [S / (name + '.cpp') for name in
                    ('GameHistory', 'GameSession', 'RecentGamesView', 'Launcher', 'ProcessLaunch', 'Renderer', 'Font', 'Theme', 'ThemeRender', 'Log')]:
                obj = args.out / (arch + '-' + source.stem + '.o')
                run(arch + '-compile-' + source.stem, [cxx, *flags, '-c', str(source), '-o', str(obj)], True)
                objects.append(str(obj))
            binary = args.out / (arch + '-test')
            run(arch + '-link', [cxx, *link, '-Wl,--gc-sections', *objects,
                '-lSDL2_image', '-lSDL2_ttf', '-lSDL2', '-ldl', '-lpng', '-ljson-c', '-lz', '-lwebp', '-o', str(binary)], True)
            run(arch + '-run', [*prefix, str(binary), str(args.out / (arch + '-fixtures')),
                               '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'])
        report['passed'] = True
    finally:
        report['inputs_after'] = hashes()
        report['source_drift'] = report['inputs_before'] != report['inputs_after']
        report['passed'] = report['passed'] and not report['source_drift']
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    assert report['passed']


if __name__ == '__main__':
    main()
