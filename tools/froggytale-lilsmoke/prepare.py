#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Prepare only caller-supplied private data; never fetch a game or atlas."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--wad', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True, help='New private data directory (must not exist)')
    a = p.parse_args()
    if a.wad.is_symlink() or not a.wad.is_file() or a.wad.stat().st_size > 128*1024*1024:
        p.error('Expected a regular, trusted WAD no larger than 128 MiB')
    with a.wad.open('rb') as f:
        head = f.read(18)
    if head[:4] != b'FORM' or head[8:12] != b'GEN8' or head[17] != 16:
        p.error('This first port admits only GameMaker WAD16, not an EXE/YYC/GMRT game')
    if struct.unpack_from('<I', head, 4)[0]+8 != a.wad.stat().st_size:
        p.error('Truncated or inconsistent FORM length')
    a.out.mkdir(parents=True, exist_ok=False)
    target = a.out / 'game.unx'
    shutil.copyfile(a.wad, target)
    subprocess.run([sys.executable, str(HERE / '_build/upstream/tools/pack_assets.py'),
                    '--datawin', str(target), '--out', str(a.out), '--atlas-cap', '320'], check=True)
    manifest = {name: hashlib.sha256((a.out/name).read_bytes()).hexdigest()
                for name in ('game.unx', 'ATLAS.BIN', 'ATLAS.RGBA4444')}
    (a.out/'private-data.json').write_text(json.dumps(dict(private_game_data=True, sha256=manifest), indent=2)+'\n')
    print(f'PRIVATE derived game data only: {a.out}. Do not include it in a public package.')


if __name__ == '__main__':
    main()
