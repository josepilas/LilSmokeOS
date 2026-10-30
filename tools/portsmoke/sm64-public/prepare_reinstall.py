#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Restage the reviewed public package with the accepted extraction fixes."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import zipfile

from public_package import package
from review_distribution import review

BASE_SHA256 = '0a8c6830a4f13a3e55ff3d01f03ad3c13ed10d062ba3aae37656c8c92f0c3fe5'
FIXES = {
    'runtime/cook.py': '682677661176cd9b72a9e1a069ad58f4c9d16e83c15f21d1024d6e112a8410c7',
    'runtime/native_build.py': '7ad4a7180d6b76ed915315b8bb9581cf7b8c6272ba56736e83466a03b28731a2',
    'runtime/bin/sm64-prepare': 'afeff47de6a79e796e56da444f0b57ebaacc61cb13572ea6ce1751e31611b4a5',
}


def digest(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def prepare(base, output):
    output = output.resolve()
    if os.name == 'nt' and not str(output).startswith('\\\\?\\'):
        output = Path('\\\\?\\' + str(output))
    root = Path(__file__).resolve().parents[3]
    qa = root / 'build/sm64-extraction-qa-20261014'
    if digest(base) != BASE_SHA256:
        raise ValueError('Base package differs from the reviewed asset-free release')
    for name in ('full-preparation-report.json', 'report.json', 'gameplay-report.json'):
        if json.loads((qa / name).read_text(encoding='utf-8-sig')).get('passed') is not True:
            raise ValueError('Missing successful acceptance test: ' + name)
    payloads = {
        'runtime/cook.py': Path(__file__).with_name('cook.py'),
        'runtime/native_build.py': Path(__file__).with_name('native_build.py'),
        'runtime/bin/sm64-prepare': qa / 'sm64-prepare',
    }
    for name, path in payloads.items():
        if digest(path) != FIXES[name]:
            raise ValueError('Tested extraction payload changed: ' + name)
    output.mkdir(parents=True, exist_ok=False)
    app = output / 'Super Mario 64'
    app.mkdir()
    with zipfile.ZipFile(base) as archive:
        seen = set()
        for item in archive.infolist():
            name = PurePosixPath(item.filename)
            if (item.is_dir() or name.is_absolute() or '..' in name.parts or
                    '\\' in item.filename or ':' in item.filename or
                    item.filename.casefold() in seen or
                    (item.external_attr >> 16) & 0o170000 == 0o120000):
                raise ValueError('Unsafe distribution entry: ' + item.filename)
            seen.add(item.filename.casefold())
            target = app.joinpath(*name.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(item) as source, target.open('xb') as dest:
                shutil.copyfileobj(source, dest, 65536)
    # Validate every original distribution file before changing the accepted three.
    manifest = json.loads((app / 'runtime/distribution-manifest.json').read_text())
    for name, entry in manifest['files'].items():
        path = app / name
        if path.stat().st_size != entry['bytes'] or digest(path) != entry['sha256']:
            raise ValueError('Base distribution readback mismatch: ' + name)
    for name, path in payloads.items():
        shutil.copyfile(path, app / name)
    launcher = Path(__file__).with_name('app') / 'SM64.sh'
    if digest(launcher) != '01161f7a8e5cc81e84f0bb5643d601dfd22104fd7b8755b5af658f5c7369b48c':
        raise ValueError('Tested launcher changed')
    shutil.copyfile(launcher, app / 'SM64.sh')
    meta = json.loads((app / 'meta.json').read_text())
    meta['version'] = '0.0.1-fix.20261015'
    meta['release_label'] = 'Local extraction fix 2026-10-15'
    (app / 'meta.json').write_text(json.dumps(meta, indent=2) + '\n')
    review(app)
    accepted = json.loads((qa / 'distribution-manifest-ready.json').read_text())['files']
    rebuilt = json.loads((app / 'runtime/distribution-manifest.json').read_text())['files']
    differences = {name for name in accepted.keys() | rebuilt.keys()
                   if accepted.get(name) != rebuilt.get(name)}
    if differences != {'meta.json'}:
        raise ValueError('Reinstall differs from accepted runtime: ' + repr(differences))
    result = package(app, output / 'SM64-LilSmoke-0.0.1-fix.20261015.lilpck')
    result['app'] = str(app)
    result['acceptance_tests_passed'] = True
    (output / 'package-report.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.base, args.output), indent=2))
