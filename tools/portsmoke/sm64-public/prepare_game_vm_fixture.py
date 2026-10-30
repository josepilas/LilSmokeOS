#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Private gameplay fixture from a freshly verified owner-ROM cache; never publish."""
import argparse
import json
from pathlib import Path
import shutil

from cook import Cache, digest_file
from native_build import NativeBuild

HERE = Path(__file__).resolve().parent
P = HERE.parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared-app', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    source = args.prepared_app.resolve()
    builder = NativeBuild(source)
    cache = Cache(source, builder.identity)
    game = cache.current()
    if game is None:
        raise ValueError('Fresh preparation has not committed a verified game')
    args.out.mkdir(parents=True, exist_ok=False)
    app = args.out / 'Super Mario 64'
    app.mkdir()
    shutil.copytree(source / 'runtime', app / 'runtime',
                    ignore=shutil.ignore_patterns('corresponding-source', '__pycache__'), symlinks=True)
    for name in ('cook.py', 'native_build.py'):
        shutil.copyfile(HERE / name, app / 'runtime' / name)
    shutil.copyfile('/root/lilsmoke-public-bootstrap/sm64-prepare', app / 'runtime/bin/sm64-prepare')
    (app / 'runtime/bin/sm64-prepare').chmod(0o755)
    for name in ('meta.json', 'SM64.sh', 'original/README.txt'):
        destination = app / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(HERE / 'app' / name, destination)
    (app / 'SM64.sh').chmod(0o755)
    shutil.copyfile(P / 'package/portsmoke-sm64/src/icon.webp', app / 'icon.webp')
    ready = app / '.sm64-cache' / game.parent.name
    ready.parent.mkdir()
    shutil.copytree(game.parent, ready)
    shutil.copyfile(cache.root / 'current.json', ready.parent / 'current.json')
    manifest = app / 'runtime/distribution-manifest.json'
    record = json.loads(manifest.read_text())
    for name in ('runtime/cook.py', 'runtime/native_build.py', 'runtime/bin/sm64-prepare'):
        record['files'][name]['sha256'] = digest_file(app / name)
        record['files'][name]['bytes'] = (app / name).stat().st_size
    manifest.write_text(json.dumps(record, indent=2) + '\n')
    assert Cache(app, builder.identity).current() == ready / 'game'
    print('Private original-engine fixture ready; no ROM in original/, never redistribute')


if __name__ == '__main__':
    main()
