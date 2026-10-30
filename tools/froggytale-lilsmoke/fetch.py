#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Fetch a bounded, pinned source subset, verifying Git blob IDs, serially."""
import hashlib
import json
from pathlib import Path
import time
import urllib.request

HERE = Path(__file__).resolve().parent


def selected(path):
    p = Path(path)
    if path in ('LICENSE', 'README.md', 'CMakeLists.txt', 'cores/butterscotch/Makefile',
                'tools/pack_assets.py'):
        return True
    if path.startswith('src/') and len(p.parts) == 2:
        return p.suffix in ('.c', '.h')
    prefixes = ('src/sw_renderer/', 'src/image/', 'src/debug_font/', 'src/physics/',
                'src/video/', 'src/cli/', 'src/audio/miniaudio/',
                'vendor/base64/', 'vendor/bzip2/', 'vendor/md5/', 'vendor/miniz/',
                'vendor/sha1/', 'vendor/stb/', 'vendor/miniaudio/', 'compat/')
    return path.startswith(prefixes) and p.suffix.lower() in ('.c', '.h', '.txt', '.md', '')


def get(url):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers={
                    'User-Agent': 'LilSmoke-FroggyTale-source-only'}), timeout=45) as r:
                data = r.read(16 * 1024 * 1024 + 1)
                if len(data) > 16 * 1024 * 1024:
                    raise ValueError('source response exceeds cap')
                return data
        except OSError:
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)


def main():
    lock = json.loads((HERE / 'upstream.lock.json').read_text())
    revision = lock['revision']
    out = HERE / '_build/upstream'
    out.mkdir(parents=True, exist_ok=True)
    inventory = out / 'source-inventory.json'
    if inventory.exists():
        tree = json.loads(inventory.read_text())
        assert tree['revision'] == revision
        files = tree['files']
    else:
        tree = json.loads(get(f'https://api.github.com/repos/Synaps33/FroggyTale/git/trees/{revision}?recursive=1'))
        assert not tree.get('truncated') and tree['sha'] == revision
        files = [x for x in tree['tree'] if x['type'] == 'blob' and selected(x['path'])]
        assert sum(x['size'] for x in files) < 16 * 1024 * 1024
        inventory.write_text(json.dumps(dict(revision=revision, files=files), indent=2) + '\n')
    for i, item in enumerate(files):
        name, sha = item['path'], item['sha']
        assert selected(name) and '..' not in Path(name).parts
        dest = out / name
        data = dest.read_bytes() if dest.exists() else get(
            f'https://raw.githubusercontent.com/Synaps33/FroggyTale/{revision}/{name}')
        actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        if actual != sha or len(data) != item['size']:
            raise ValueError(f'source identity mismatch: {name}')
        if not dest.exists():
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        if i % 25 == 0:
            print(f'verified {i+1}/{len(files)}: {name}', flush=True)
    print(f'verified {len(files)} source files ({sum(x["size"] for x in files)} bytes); no game assets')


if __name__ == '__main__':
    main()
