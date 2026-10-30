#!/usr/bin/env python3
"""Fail-closed artifact review. No uploading, extraction, execution or deletion."""
# SPDX-License-Identifier: MPL-2.0
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import zipfile

ROOT=Path(__file__).resolve().parents[2]
VERSION=(ROOT/'VERSION').read_text(encoding='utf-8').strip()
DENIED_PARTS={'_build','test-results','artifacts','stock-export','private-local-apps','SD_RECUPERADO'}
GAME_SUFFIXES={'.z64','.n64','.v64','.sfc','.smc','.gba','.gb','.gbc','.nds','.mpq','.wad','.iso','.chd','.cue'}
UNREVIEWED_ARCHIVES={'.7z','.rar','.gz','.zst','.xz','.tar','.syspck','.opk'}
TEXT_SUFFIXES={'.md','.txt','.rst','.py','.c','.cpp','.h','.hpp','.sh','.mk','.in','.yml','.yaml','.json','.map','.cfg','.conf','.lock','.defconfig'}
TEXT_NAMES={'LICENSE','COPYING','VERSION','Makefile','.gitignore'}

def small_json(path,maximum=1024*1024):
    if path.stat().st_size>maximum: raise ValueError('metadata exceeds budget')
    def pairs(items):
        out={}
        for key,value in items:
            if key in out: raise ValueError('duplicate metadata key')
            out[key]=value
        return out
    return json.loads(path.read_text(encoding='utf-8-sig'),object_pairs_hook=pairs)

def package_reasons(path):
    reasons=[]
    try:
        with zipfile.ZipFile(path) as archive:
            entries=archive.infolist()
            if len(entries)>4096 or sum(e.file_size for e in entries)>256*1024*1024:
                return ['Archive exceeds review limits']
            names=set()
            for entry in entries:
                name=entry.filename
                parts=PurePosixPath(name).parts
                if name.casefold() in names or name.startswith('/') or '\\' in name or '..' in parts:
                    reasons.append('Unsafe or duplicate archive member');continue
                names.add(name.casefold())
                mode=entry.external_attr>>16
                if stat.S_ISLNK(mode): reasons.append('Archive contains links')
                if Path(name).suffix.lower() in GAME_SUFFIXES or any(p in DENIED_PARTS for p in parts):
                    reasons.append('Private/game-data archive member')
                if name in ('meta.json','runtime/build-info.json'):
                    if entry.file_size>65536: reasons.append('Oversized package metadata');continue
                    data=json.loads(archive.read(entry))
                    if not isinstance(data,dict): raise ValueError('package metadata must be an object')
                    if data.get('private_user_build') or data.get('contains_user_rom_assets') or data.get('public_distribution_allowed') is False:
                        reasons.append('Package explicitly marked private or contains extracted game data')
                if 'graphics-source-only' in name:
                    reasons.append('Binary uses source-only graphics implementation')
                if name.endswith(('sm64-original','sm64.us.f3dex2e')):
                    reasons.append('Full local SM64 executable is not an approved public BYO artifact')
                if not entry.is_dir():
                    with archive.open(entry) as stream: header=stream.read(512)
                    if header.startswith((b'\x7fELF',b'MZ',b'\x27\x05\x19\x56',b'\xd0\x0d\xfe\xed')):
                        reasons.append('Archive executable/firmware requires corresponding-source and license clearance')
                    if header.startswith((b'\x80\x37\x12\x40',b'\x37\x80\x40\x12',b'\x40\x12\x37\x80')):
                        reasons.append('Archive contains a renamed N64 ROM')
                    if header.startswith((b'PK\x03\x04',b'7z\xbc\xaf\x27\x1c',b'Rar!',b'\x1f\x8b',b'\xfd7zXZ\x00',b'\x28\xb5\x2f\xfd')):
                        reasons.append('Nested compressed container needs per-member review')
    except (OSError,ValueError,KeyError,zipfile.BadZipFile,RuntimeError) as error:
        reasons.append('Archive cannot be reviewed safely: '+str(error))
    return sorted(set(reasons))

def artifact_reasons(path):
    path=Path(path)
    info=path.lstat()
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&getattr(stat,'FILE_ATTRIBUTE_REPARSE_POINT',0):
        return ['Artifact is not a plain regular file']
    reasons=[]
    if any(p in DENIED_PARTS for p in path.parts): reasons.append('Owner-local artifact directory')
    suffix=path.suffix.lower()
    if suffix in GAME_SUFFIXES: reasons.append('ROM/BIOS/game-data image format excluded')
    if suffix not in TEXT_SUFFIXES|{'.zip','.lilpck'} and path.name not in TEXT_NAMES:
        reasons.append('Artifact format is not in the reviewed source/documentation allowlist')
    if suffix in UNREVIEWED_ARCHIVES: reasons.append('Compressed container needs an approved per-member release review')
    with path.open('rb') as stream: magic=stream.read(512)
    if magic.startswith((b'\x80\x37\x12\x40',b'\x37\x80\x40\x12',b'\x40\x12\x37\x80')):
        reasons.append('Renamed N64 ROM excluded')
    if magic.startswith((b'7z\xbc\xaf\x27\x1c',b'Rar!',b'\x1f\x8b',b'\xfd7zXZ\x00',b'\x28\xb5\x2f\xfd')):
        reasons.append('Renamed compressed container needs per-member review')
    if suffix=='.img' or (len(magic)==512 and magic[510:512]==b'\x55\xaa'):
        reasons.append('Public flashable image clearance is pending; filename changes cannot bypass this gate')
        report_path=path.with_suffix('.img.json')
        if report_path.is_file():
            try:
                report=small_json(report_path)
                if report.get('stock_hashes') or report.get('source_img_sha256'):
                    reasons.append('Image derived from owner stock boot/kernel/vendor files')
            except (OSError,ValueError): reasons.append('Invalid image provenance metadata')
    if suffix in ('.lilpck','.zip') or magic.startswith(b'PK\x03\x04'):
        reasons+=package_reasons(path)
    if suffix in ('.exe','.so','.a','.bin') or magic.startswith(b'\x7fELF') or magic.startswith(b'MZ'):
        reasons.append('Binary requires exact corresponding source and dependency/license clearance')
    if magic.startswith((b'\xd0\x0d\xfe\xed',b'\x27\x05\x19\x56')):
        reasons.append('Board/firmware binary is not cleared for public redistribution')
    return sorted(set(reasons))

def audit(paths):
    rows=[]
    for path in paths:
        path=Path(path)
        try:
            reasons=artifact_reasons(path)
        except (OSError,ValueError) as error: reasons=['Unreadable artifact: '+str(error)]
        rows.append(dict(artifact=path.name,publishable=not reasons,reasons=reasons))
    return dict(version=VERSION,passed=bool(rows) and all(r['publishable'] for r in rows),
        upload_performed=False,legal_clearance_automated=False,manual_redistribution_review_required=True,artifacts=rows)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts',nargs='+',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    result=audit(args.artifacts)
    text=json.dumps(result,indent=2)+'\n'
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(text,encoding='utf-8')
    print(text,end='')
    return 0 if result['passed'] else 2

if __name__=='__main__': raise SystemExit(main())
