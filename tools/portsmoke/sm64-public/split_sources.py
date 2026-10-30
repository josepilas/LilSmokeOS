#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Move distribution-only sources out of a reviewed release without changing its runtime."""
import argparse
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import zipfile

from review_distribution import source_offer

SOURCE_PREFIX='runtime/corresponding-source/'
SOURCE_URL='https://github.com/josepilas/SM64-LilSmoke/releases/download/Pre-Alpha-0.0.1/corresponding-source.zip'


def sha(path):
    with path.open('rb') as handle: return hashlib.file_digest(handle,'sha256').hexdigest()


def verified_members(archive,manifest):
    seen=set()
    expected=set(manifest['files'])|{'runtime/distribution-manifest.json'}
    for info in archive.infolist():
        name=PurePosixPath(info.filename)
        if name.is_absolute() or '..' in name.parts or '\\' in info.filename:
            raise ValueError('Unsafe archive path')
        if info.is_dir() or info.filename.casefold() in seen or (info.external_attr>>16)&0o170000==0o120000:
            raise ValueError('Duplicate, linked or unexpected archive member')
        seen.add(info.filename.casefold())
        if info.filename not in expected: raise ValueError('Unreviewed release member')
        if info.filename in manifest['files']:
            row=manifest['files'][info.filename]
            with archive.open(info) as handle: digest=hashlib.file_digest(handle,'sha256').hexdigest()
            if info.file_size!=row['bytes'] or digest!=row['sha256']:
                raise ValueError('Release member failed checksum: '+info.filename)
    if len(seen)!=len(expected): raise ValueError('Reviewed release member missing')


def copy_member(source,name,destination):
    with source.open(name) as reader, destination.open(name,'w',force_zip64=True) as writer:
        shutil.copyfileobj(reader,writer,128*1024)


def add_project_snapshot(checkout,destination):
    git=['git','-c','safe.directory='+str(checkout.resolve()),'-C',str(checkout)]
    commit=subprocess.check_output([*git,'rev-parse','HEAD'],text=True).strip()
    process=subprocess.Popen([*git,'archive',commit],stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=process.stdout,mode='r|') as source:
            for member in source:
                if member.isdir(): continue
                path=PurePosixPath(member.name)
                if not member.isfile() or path.is_absolute() or '..' in path.parts:
                    raise ValueError('Unsafe project source member')
                if path.suffix.lower() in ('.z64','.n64','.v64','.m64','.aiff','.aifc','.pdat','.lilpck'):
                    raise ValueError('Private game data in project snapshot')
                with source.extractfile(member) as reader:
                    prefix=reader.read(8)
                    if prefix.startswith((b'\x7fELF',b'MZ',b'!<arch>\n')):
                        raise ValueError('Compiled binary in project source snapshot')
                    with destination.open('project-source/'+member.name,'w',force_zip64=True) as writer:
                        writer.write(prefix)
                        shutil.copyfileobj(reader,writer,128*1024)
        if process.wait()!=0: raise RuntimeError('Project source archive failed')
    finally:
        process.stdout.close()
        if process.poll() is None: process.terminate();process.wait()
    return commit


def split(original,out,checkout=None):
    out.mkdir(parents=True,exist_ok=True)
    source_zip=out/'corresponding-source.zip'
    package=out/original.name
    if source_zip.exists() or package.exists(): raise ValueError('Output artifacts already exist')
    with zipfile.ZipFile(original) as source:
        manifest=json.loads(source.read('runtime/distribution-manifest.json'))
        original_recipe=source.read('runtime/recipe.json')
        verified_members(source,manifest)
        external={name:row for name,row in manifest['files'].items() if name.startswith(SOURCE_PREFIX)}
        if not external: raise ValueError('No bundled corresponding source to separate')
        commit=None
        with zipfile.ZipFile(source_zip,'x',zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as destination:
            for name in sorted(external): copy_member(source,name,destination)
            if checkout is not None: commit=add_project_snapshot(checkout,destination)
            destination.writestr('SOURCE-MANIFEST.json',json.dumps(dict(schema=1,
                original_package_sha256=sha(original),dependency_files=external,
                project_source_commit=commit,rom_included=False,cooked_game_included=False),indent=2)+'\n')
            destination.writestr('README.txt',
                'Matching dependency sources/build inputs are under runtime/corresponding-source/.\n'
                'The public project source snapshot, when included, is under project-source/.\n'
                'This ZIP is not required on the console. Do not install it as an application.\n'
                'For package rebuilding, extract it alongside the unpacked .lilpck.\n')
        delivery=dict(asset='corresponding-source.zip',url=SOURCE_URL,sha256=sha(source_zip),
            bytes=source_zip.stat().st_size,project_source_commit=commit)
        changed={'SOURCE-OFFER.txt':source_offer(delivery).encode()}
        review=json.loads(source.read('runtime/source-material-review.json'))
        review['external_corresponding_source']=delivery
        changed['runtime/source-material-review.json']=(json.dumps(review,indent=2)+'\n').encode()
        lean=copy.deepcopy(manifest)
        lean['files']={name:row for name,row in lean['files'].items() if not name.startswith(SOURCE_PREFIX)}
        for name,data in changed.items(): lean['files'][name]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
        lean['corresponding_source_delivery']=delivery
        with zipfile.ZipFile(package,'x',zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as destination:
            for name in sorted(lean['files']):
                if name in changed: destination.writestr(name,changed[name])
                else: copy_member(source,name,destination)
            destination.writestr('runtime/distribution-manifest.json',json.dumps(lean,indent=2)+'\n')
    for artifact in (package,source_zip):
        with zipfile.ZipFile(artifact) as archive:
            if archive.testzip() is not None: raise ValueError('Output archive CRC failed')
        artifact.with_suffix(artifact.suffix+'.sha256').write_text(sha(artifact)+'  '+artifact.name+'\n')
    with zipfile.ZipFile(package) as archive:
        verified_members(archive,lean)
        assert not any(name.startswith(SOURCE_PREFIX) for name in archive.namelist())
        if archive.read('runtime/recipe.json')!=original_recipe:
            raise ValueError('Runtime recipe changed while externalizing sources')
    result=dict(package=package.name,package_bytes=package.stat().st_size,package_sha256=sha(package),
        corresponding_source=delivery,previous_package_bytes=original.stat().st_size,
        installed_files=len(lean['files'])+1,operational_payload_unchanged=True,
        offline_first_run_unchanged=True,source_files_externalized=len(external))
    (out/'split-report.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--source-checkout',type=Path)
    args=parser.parse_args()
    print(json.dumps(split(args.package,args.output,args.source_checkout),indent=2))
