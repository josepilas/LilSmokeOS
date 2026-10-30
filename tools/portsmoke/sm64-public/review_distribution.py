#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Explicitly review and freeze the distribution input manifest, not the game cache."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile

HELPERS={'tools/aifc_decode','tools/aiff_extract_codebook','tools/armips','tools/mio0',
    'tools/extract_data_for_mio',
    'tools/patch_libultra_math','tools/skyconv','tools/tabledesign','tools/textconv','tools/vadpcm_enc',
    'tools/n64cksum','tools/n64graphics','tools/n64graphics_ci','tools/n64sound',
    'tools/tkmk00','tools/audiofile/audiofile.o','tools/audiofile/libaudiofile.a'}


def digest(path):
    with path.open('rb') as handle: return hashlib.file_digest(handle,'sha256').hexdigest()


def source_offer(delivery):
    prefix = 'https://github.com/josepilas/SM64-LilSmoke/releases/download/'
    url = delivery['url']
    if not url.startswith(prefix) or '/' not in url[len(prefix):]:
        raise ValueError('Corresponding-source URL must identify this project release')
    tag = url[len(prefix):].split('/', 1)[0]
    return ('SM64-LilSmoke - Corresponding Source\n\n'
        'The matching dependency sources, build inputs and project source snapshot\n'
        'are provided at no charge as a separate asset in the same release:\n\n'
        + delivery['url']+'\n\nSHA-256: '+delivery['sha256']+'\n\n'
        'Release: https://github.com/josepilas/SM64-LilSmoke/releases/tag/'+tag+'\n'
        'Public source: https://github.com/josepilas/SM64-LilSmoke\n\n'
        'Downloading corresponding-source.zip is not necessary to install or play.\n'
        'The package retains its operational engine source, native compiler and\n'
        'runtimes, so original/ ROM preparation works offline as before.\n'
        'No ROM, extracted game data or cooked game executable is supplied.\n'
        'Component copyright and license texts are retained in runtime/licenses\n'
        'and runtime/compiler/usr/share/doc. Their individual terms still apply.\n')


def review(app):
    files=['meta.json','SM64.sh','icon.webp','LICENSE','original/README.txt']
    if (app/'SOURCE-OFFER.txt').is_file(): files.append('SOURCE-OFFER.txt')
    allowed={'source.tar.gz','recipe.json','cook.py','native_build.py','resume.py','build_progress.py','sound_data.S',
        'bin/sm64-prepare','bin/sm64-bin2c','bin/sm64-build-shell','bin/python3',
        'source-material-review.json'}
    for path in (app/'runtime').rglob('*'):
        if not path.is_file() or '__pycache__' in path.parts or path.suffix=='.pyc': continue
        relative=path.relative_to(app/'runtime')
        if relative.as_posix()=='distribution-manifest.json': continue
        if relative.as_posix() not in allowed and relative.parts[0] not in (
            'compiler','python','sysroot','lib','objects','licenses','corresponding-source'):
            raise ValueError('Unexpected distribution file: '+str(relative))
        if path.is_symlink(): raise ValueError('Linked distribution input: '+str(relative))
        files.append('runtime/'+relative.as_posix())
    helpers={}
    seen={}
    collisions=[]
    for name in files:
        if name.casefold() in seen:
            collisions.append([seen[name.casefold()],name])
        seen[name.casefold()]=name
    if collisions:
        raise ValueError('FAT case collisions: '+json.dumps(collisions))
    with tarfile.open(app/'runtime/source.tar.gz','r:gz') as archive:
        for item in archive:
            if not item.isfile(): raise ValueError('Non-file source archive member')
            with archive.extractfile(item) as handle: data=handle.read()
            binary=data.startswith((b'\x7fELF',b'!<arch>\n'))
            if binary:
                if item.name not in HELPERS: raise ValueError('Unexpected source binary: '+item.name)
                helpers[item.name]=hashlib.sha256(data).hexdigest()
            elif Path(item.name).suffix.lower() in ('.z64','.v64','.n64','.png','.aiff','.aifc','.m64','.bin','.o'):
                raise ValueError('Extracted data in source: '+item.name)
    manifest=dict(schema=1,files={name:dict(sha256=digest(app/name),bytes=(app/name).stat().st_size)
        for name in sorted(files)},source_helpers=helpers,
        rom_included=False,cooked_game_included=False,renderer_binary_included=False)
    (app/'runtime/distribution-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return dict(reviewed_files=len(files),helper_exceptions=sorted(helpers),passed=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(review(args.app),indent=2))
