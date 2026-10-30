#!/usr/bin/env python3
"""Serial ASan/UBSan and SDK/MIPS Favorites and system-filter qualification."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import shutil
import subprocess

P = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    parser.add_argument('--previous', type=Path, help='Reuse verified unchanged focused objects; rebuild changed sources')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    files = sorted(p for p in (P / 'package/lilsmoke-ui').rglob('*') if p.suffix in ('.h', '.cpp', '.mk') or p.name == 'Makefile')
    for directory in ('lilsmoke-scripts/src', 'lilsmoke-media/src', 'lilsmoke-od-sdl/src'):
        files += sorted((P / 'package' / directory).glob('*.h'))
    files += [Path(__file__), P / 'tools/ui/favorites-test.cpp']
    def hashes():
        return {str(p.relative_to(P)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    report = dict(passed=False, inputs_before=hashes(), commands=[], results=[])
    previous = json.loads((args.previous / 'report.json').read_text()) if args.previous else None
    if previous and (not previous['passed'] or previous['source_drift']):
        raise ValueError('Previous focused run did not qualify')
    headers_unchanged = previous and all(previous['inputs_before'].get(key) == value
        for key, value in report['inputs_before'].items() if key.endswith('.h'))
    snapshot = args.out / 'source-snapshot'
    for relative in report['inputs_before']:
        target = snapshot / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(P / relative, target)
    source = snapshot / 'package/lilsmoke-ui/src/src'
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    def limit():
        resource.setrlimit(resource.RLIMIT_AS, (768 * 1024**2, 768 * 1024**2))
    def run(name, command, compiler=False):
        report['commands'].append(dict(name=name, argv=list(map(str, command))))
        with (args.out / (name + '.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env,
                                    preexec_fn=limit if compiler else None, timeout=600)
        report['results'].append(dict(name=name, returncode=result.returncode))
        print(name, result.returncode, flush=True)
        if result.returncode:
            print((args.out / (name + '.log')).read_text()[-6000:])
        result.check_returncode()
    try:
        for arch in ('host', 'mips'):
            cxx = 'g++' if arch == 'host' else str(args.sdk / 'host/bin/mipsel-buildroot-linux-musl-g++')
            flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                     '-ffunction-sections', '-fdata-sections', '-I' + str(source),
                     '-I' + str(snapshot / 'package/lilsmoke-scripts/src'), '-I' + str(snapshot / 'package/lilsmoke-media/src')]
            if arch == 'host':
                flags += shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl2', 'SDL2_image', 'SDL2_ttf'], text=True))
                flags += ['-fsanitize=address,undefined', '-fno-pie', '-fno-omit-frame-pointer']
                link = ['-fsanitize=address,undefined', '-no-pie']
                prefix = []
            else:
                flags += ['-I' + str(args.sdk / 'staging/usr/include'), '-include', str(snapshot / 'package/lilsmoke-od-sdl/src/input-wire.h')]
                link = []
                prefix = ['qemu-mipsel', '-cpu', '74Kf', '-L', str(args.sdk / 'target')]
            objects = []
            for path in [snapshot / 'tools/ui/favorites-test.cpp'] + [source / (name + '.cpp') for name in
                    ('FavoritesStore', 'FavoritesView', 'GameOptionsView', 'GameHistory', 'RecentGamesView', 'GridView',
                     'SettingsView', 'Config', 'ProcessLaunch', 'Renderer', 'Font', 'Theme', 'ThemeRender', 'Log')]:
                obj = args.out / (arch + '-' + path.stem + '.o')
                relative = str(path.relative_to(snapshot))
                old_obj = args.previous / obj.name if args.previous else None
                if headers_unchanged and previous['inputs_before'].get(relative) == report['inputs_before'][relative] and old_obj.is_file():
                    shutil.copyfile(old_obj, obj)
                    report.setdefault('reused_objects', []).append(dict(path=str(obj), previous=str(old_obj),
                        sha256=hashlib.sha256(obj.read_bytes()).hexdigest(), source_sha256=report['inputs_before'][relative]))
                else:
                    run(arch + '-compile-' + path.stem, [cxx, *flags, '-c', str(path), '-o', str(obj)], True)
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
