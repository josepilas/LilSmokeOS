#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Stage only reviewed distribution files, never private caches or a ROM."""
import argparse
import json
from pathlib import Path, PurePosixPath
import shutil

from cook import digest_file, regular, verify_mips_executable
from public_package import package
from review_distribution import review


def stage(base, out, helper, bootstrap, build_shell, version='0.0.1-prep.8', sdl_library=None):
    out.mkdir(parents=True, exist_ok=False)
    app = out / 'Super Mario 64'
    manifest = json.loads((base / 'runtime/distribution-manifest.json').read_text())
    for name, row in manifest['files'].items():
        relative = PurePosixPath(name)
        if (relative.is_absolute() or '..' in relative.parts or '\\' in name or
                relative.parts[0] in ('.sm64-cache', 'saves') or
                (relative.parts[0] == 'original' and name != 'original/README.txt')):
            raise ValueError('Private or unsafe distribution input')
        source = base / name
        regular(source)
        if source.stat().st_size != row['bytes'] or digest_file(source) != row['sha256']:
            raise ValueError('Reviewed base changed: ' + name)
        target = app / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(source.stat().st_mode & 0o777)
    here = Path(__file__).resolve().parent
    old_launcher = (app / 'SM64.sh').read_text()
    launcher = (here / 'app/SM64.sh').read_text()
    old_limit = launcher.replace('96 MiB virtual limit', '64 MiB limit')
    previous = old_limit.replace(
        '# Virtual headroom includes compiler file mappings, not just resident RAM.\n'
        '# The game drops to its separate 64 MiB limit after worker/UI exit.\n'
        'ulimit -v 131072',
        '# Preparation may use up to 90 MiB virtual; the game runs after worker/UI exit.\n'
        'ulimit -v 92160')
    if old_launcher not in (launcher, old_limit, previous):
        raise ValueError('Unexpected application launcher; refusing unrelated replacement')
    (app / 'SM64.sh').write_text(launcher)
    for name in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py', 'sound_data.S'):
        shutil.copyfile(here / name, app / 'runtime' / name)
    for source, name in ((helper, 'sm64-bin2c'), (bootstrap, 'sm64-prepare'), (build_shell, 'sm64-build-shell')):
        verify_mips_executable(source)
        target = app / 'runtime/bin' / name
        shutil.copyfile(source, target)
        target.chmod(0o755)
    for name in ('lilsmoke-input.o', 'lilsmoke-common-input.o', 'lilsmoke-keymap.o', 'lilsmoke-log.o'):
        source = bootstrap.parent / 'objects' / name
        regular(source, 4 * 1024 * 1024)
        shutil.copyfile(source, app / 'runtime/objects' / name)
    if sdl_library is not None:
        regular(sdl_library, 8 * 1024 * 1024)
        with sdl_library.open('rb') as source:
            header = source.read(20)
        if header[:7] != b'\x7fELF\x01\x01\x01' or header[16:20] != b'\x03\x00\x08\x00':
            raise ValueError('SDL must be a MIPS32 little-endian shared library')
        for name in ('lib/libSDL2-2.0.so.0', 'sysroot/usr/lib/libSDL2.so'):
            shutil.copyfile(sdl_library, app / 'runtime' / name)
    meta = json.loads((app / 'meta.json').read_text())
    meta['version'] = version
    meta['release_label'] = 'Verified cached launch and faster offline preparation'
    (app / 'meta.json').write_text(json.dumps(meta, indent=2) + '\n')
    review(app)
    result = package(app, out / ('SM64-LilSmoke-' + version + '.lilpck'))
    result['app'] = str(app)
    (out / 'package-report.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--helper', type=Path, required=True)
    parser.add_argument('--bootstrap', type=Path, required=True)
    parser.add_argument('--build-shell', type=Path, required=True)
    parser.add_argument('--sdl-library', type=Path)
    args = parser.parse_args()
    print(json.dumps(stage(args.base, args.out, args.helper, args.bootstrap, args.build_shell,
        sdl_library=args.sdl_library), indent=2))
