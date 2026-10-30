#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Stage the public SM64 source checkout without ROMs, caches or game binaries."""
import argparse
import json
from pathlib import Path
import shutil
import tarfile

HERE=Path(__file__).resolve().parent
P=HERE.parents[2]
DENIED={'.png','.z64','.v64','.n64','.aiff','.aifc','.m64','.bin','.o','.a','.so','.exe','.pdat'}


def copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.is_file() and source.read_bytes()==target.read_bytes(): return
    shutil.copyfile(source,target)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--refresh',action='store_true')
    parser.add_argument('--status-file',type=Path,default=HERE/'progress.json')
    parser.add_argument('--input-source',type=Path,default=P/'package/lilsmoke-ui/src/src',
        help='Exact input source snapshot matching the shipped authored objects')
    args=parser.parse_args()
    root=args.output
    assert root.is_dir() and (root/'.git').is_dir()
    assert args.refresh or not (root/'src').exists(),'Public source staging must be new'
    readme=(HERE/'source_README.md').read_text()
    existing=root/'README.md'
    if existing.is_file():
        previous=existing.read_text()
        banner=previous.split('# SM64-LilSmoke',1)[0]
        if banner.strip() and not readme.startswith(banner):
            readme=banner+readme
    existing.write_text(readme)
    copy(HERE/'source_Makefile',root/'Makefile')
    copy(P/'LICENSES/MPL-2.0.txt',root/'LICENSE')
    for path in (args.app/'runtime/licenses').iterdir():
        if path.is_file(): copy(path,root/'LICENSES'/path.name)
    copy(Path('/root/lilsmoke-sm64-base/LICENSE.md'),root/'engine/UPSTREAM-CC0.txt')
    copy(args.status_file,root/'RELEASE-STATUS.json')
    if (HERE/'CHANGELOG.md').is_file(): copy(HERE/'CHANGELOG.md',root/'CHANGELOG.md')
    for path in (HERE/'app').rglob('*'):
        if path.is_file(): copy(path,root/'app'/path.relative_to(HERE/'app'))
    copy(args.app/'icon.webp',root/'app/icon.webp')
    for name in ('bootstrap.cpp','cook.py','native_build.py','public.mk',
                 'resume.py','build_progress.py','build_shell.c','bin2c.c','sound_data.S'):
        copy(HERE/name,root/'src/prepare'/name)
    for path in (P/'package/lilsmoke-sm64-original/src').iterdir():
        if path.is_file(): copy(path,root/'src/adaptation'/path.name)
    for name in ('m8-audio.c','m8-audio.h'):
        copy(P/'board/segam-m8-v6-stock'/name,root/'src/adaptation'/name)
    for name in ('m8_pocketsnes_audio.c','m8_pocketsnes_audio.h'):
        copy(P/'package/opendingux-emulators'/name,root/'src/adaptation'/name)
    for name in ('Input.cpp','Input.h','Keymap.cpp','Keymap.h','Log.cpp','Log.h','gamepad-state.h'):
        copy(args.input_source/name,root/'src/input'/name)
    copy(P/'package/lilsmoke-od-sdl/src/input-wire.h',root/'src/input/input-wire.h')
    copy(P/'package/portsmoke-sm64/src/vendor/tinygl/src/font8x8_basic.h',root/'src/prepare/font8x8_basic.h')
    for path in HERE.glob('test_*.py'): copy(path,root/'tests'/path.name)
    copy(HERE/'sound_data.S',root/'tests/sound_data.S')
    copy(HERE/'cook.py',root/'tests/cook.py')
    for name in ('independent_objects.py','audit_completed_progress.py',
                 'audit_independent_candidates.py','export_clock_update.py',
                 'export_preparation_update.py','reuse_vm_compiles.py'):
        copy(HERE/name,root/'tools'/name)
    copy(HERE/'public_package.py',root/'tools/package.py')
    copy(HERE/'audit_runtime.py',root/'tools/audit_runtime.py')
    copy(HERE/'review_distribution.py',root/'tools/review_distribution.py')
    copy(HERE/'split_sources.py',root/'tools/split_sources.py')
    copy(HERE/'prepare_release.py',root/'tools/prepare_release.py')
    for path in (HERE/'compiler').iterdir():
        if path.is_file() and path.suffix in ('.c','.py','.sh','.sources'):
            copy(path,root/'tools/compiler'/path.name)
    count=0
    with tarfile.open(args.app/'runtime/source.tar.gz','r:gz') as archive:
        for item in archive:
            if not item.isfile(): continue
            relative=Path(item.name)
            if relative.is_absolute() or '..' in relative.parts: raise ValueError('Unsafe staged source')
            if relative.suffix.lower() in DENIED or 'build' in relative.parts: continue
            data=archive.extractfile(item).read()
            if data.startswith((b'\x7fELF',b'MZ',b'!<arch>\n')): continue
            target=root/'engine'/relative
            target.parent.mkdir(parents=True,exist_ok=True)
            if not target.is_file() or target.read_bytes()!=data:
                target.write_bytes(data)
            count+=1
    (root/'.gitignore').write_text('/build/\n/original/\n.sm64-cache/\nsaves/\n__pycache__/\n*.pyc\n*.lilpck\n')
    (root/'UPSTREAM.json').write_text(json.dumps(dict(engine_source_pin='a173bd0a3eeed9b064a234e528f128424d46ea08',
        source_files=count,rom_included=False,extracted_assets_included=False,game_binary_included=False,
        upstream_graphics_binary_redistribution_allowed=False,release_acceptance_pending=True),indent=2)+'\n')
    print(json.dumps(dict(source_files=count,public_source_staged=True,uploaded=False),indent=2))


if __name__=='__main__': main()
