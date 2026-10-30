#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Rebuild an asset-free .lilpck from this checkout and a reviewed runtime template."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

DENIED={'.png','.z64','.n64','.v64','.aiff','.aifc','.m64','.bin','.o','.exe','.pdat'}
OBJECTS=('lilsmoke-input.o','lilsmoke-common-input.o','lilsmoke-keymap.o','lilsmoke-log.o')


def package(app, output):
    """Whitelist distribution inputs; never walk the game's original/cache/saves."""
    manifest_path=app/'runtime/distribution-manifest.json'
    source_review=json.loads((app/'runtime/source-material-review.json').read_text())
    if source_review.get('schema')!=1 or source_review.get('complete') is not True:
        raise ValueError('Dependency source/material review is incomplete')
    manifest=json.loads(manifest_path.read_text())
    if manifest.get('schema')!=1: raise ValueError('Unsupported distribution manifest')
    files=[]
    for name,row in manifest['files'].items():
        relative=Path(name)
        if relative.is_absolute() or '..' in relative.parts or '\\' in name:
            raise ValueError('Unsafe distribution manifest path')
        path=app/relative
        with path.open('rb') as handle: digest=hashlib.file_digest(handle,'sha256').hexdigest()
        if digest!=row['sha256'] or path.stat().st_size!=row['bytes']:
            raise ValueError('Distribution input changed: '+name)
        files.append(path)
    files.append(manifest_path)
    for path in files:
        if not path.is_file() or path.is_symlink():
            raise ValueError('Missing or linked package input: '+str(path))
        if any(part in ('.sm64-cache','saves','__pycache__') for part in path.relative_to(app).parts):
            raise ValueError('Private cache or generated files in distribution inputs')
    with tarfile.open(app/'runtime/source.tar.gz','r:gz') as source:
        for member in source:
            if not member.isfile(): continue
            handle=source.extractfile(member)
            data=handle.read()
            expected=manifest.get('source_helpers',{}).get(member.name)
            if expected:
                if hashlib.sha256(data).hexdigest()!=expected:
                    raise ValueError('Unreviewed source helper')
                continue
            if Path(member.name).suffix.lower() in DENIED or 'build' in Path(member.name).parts:
                raise ValueError('ROM, extracted data or cooked objects in engine source')
            if data.startswith((b'\x7fELF',b'!<arch>\n')):
                raise ValueError('Game/renderer binary in source archive')
    output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(output,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as archive:
        for path in files:
            archive.write(path,path.relative_to(app).as_posix())
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError('Package readback checksum failed')
    with output.open('rb') as handle:
        digest=hashlib.file_digest(handle,'sha256').hexdigest()
    output.with_suffix(output.suffix+'.sha256').write_text(digest+'  '+output.name+'\n')
    return dict(package=output.name,bytes=output.stat().st_size,sha256=digest,
        files=len(files),rom_included=False,cooked_game_included=False,
        renderer_binary_included=False,root_script='SM64.sh')


def rebuild(root, template, sdk, app):
    cross=sdk/'bin/mipsel-buildroot-linux-musl-'
    subprocess.run(['make','-j1','SDK='+str(sdk)],cwd=root,check=True)
    template_materials=json.loads((template/'source-material-review.json').read_text())
    ignored=['__pycache__','*.pyc']
    if template_materials.get('external_corresponding_source'): ignored.append('corresponding-source')
    shutil.copytree(template,app/'runtime',ignore=shutil.ignore_patterns(*ignored))
    for path in (root/'app').rglob('*'):
        if path.is_file():
            target=app/path.relative_to(root/'app')
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(path,target)
    shutil.copyfile(root/'LICENSE',app/'LICENSE')
    materials=json.loads((app/'runtime/source-material-review.json').read_text())
    if materials.get('external_corresponding_source'):
        from review_distribution import source_offer
        (app/'SOURCE-OFFER.txt').write_text(source_offer(materials['external_corresponding_source']))
    # The runtime template retains the reviewed user-provided WebP icon.
    shutil.copyfile(root/'app/icon.webp',app/'icon.webp')
    shutil.copyfile(root/'build/sm64-prepare',app/'runtime/bin/sm64-prepare')
    for name in ('sm64-bin2c','sm64-build-shell'):
        shutil.copyfile(root/'build'/name,app/'runtime/bin'/name)
    for name in ('cook.py','native_build.py','resume.py','build_progress.py','sound_data.S'):
        shutil.copyfile(root/'src/prepare'/name,app/'runtime'/name)
    for name in OBJECTS:
        shutil.copyfile(root/'build'/name,app/'runtime/objects'/name)
    with tempfile.TemporaryDirectory(prefix='sm64-source-') as temporary:
        source=Path(temporary)/'engine'
        shutil.copytree(root/'engine',source)
        for path in source.rglob('*'):
            if path.is_file() and (path.suffix.lower() in DENIED or 'build' in path.relative_to(source).parts):
                raise ValueError('Engine checkout contains generated data')
        for path in (root/'src/adaptation').iterdir():
            if path.is_file(): shutil.copyfile(path,source/'lilsmoke'/path.name)
        shutil.copyfile(root/'src/prepare/public.mk',source/'lilsmoke/public.mk')
        subprocess.run(['make','-j1','CC='+str(cross)+'gcc','CXX='+str(cross)+'g++'],
            cwd=source/'tools',check=True)
        with tarfile.open(app/'runtime/source.tar.gz','w:gz') as archive:
            for path in sorted(source.rglob('*')):
                if path.is_file() and path.suffix.lower() not in DENIED:
                    archive.add(path,arcname=path.relative_to(source).as_posix(),recursive=False)
    recipe_path=app/'runtime/recipe.json'
    recipe=json.loads(recipe_path.read_text())
    with (app/'runtime/source.tar.gz').open('rb') as handle:
        recipe['source_sha256']=hashlib.file_digest(handle,'sha256').hexdigest()
    recipe_path.write_text(json.dumps(recipe,indent=2)+'\n')
    from review_distribution import review
    review(app)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,help='Package an already reviewed asset-free staging folder')
    parser.add_argument('--runtime',type=Path,help='runtime/ from the asset-free release, not a game cache')
    parser.add_argument('--sdk',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.app:
        result=package(args.app,args.output)
    else:
        if not args.runtime or not args.sdk:
            parser.error('Provide --app, or both --runtime and --sdk')
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='sm64-public-package-') as temporary:
            app=Path(temporary)/'Super Mario 64'
            rebuild(root,args.runtime,args.sdk,app)
            result=package(app,args.output)
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
