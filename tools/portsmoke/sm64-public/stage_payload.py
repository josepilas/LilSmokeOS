#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build an asset-free first-run candidate from clean pinned source, never private game objects."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
from audit_runtime import audit

HERE=Path(__file__).resolve().parent
P=HERE.parents[2]
SDK=Path('/root/lilsmoke-os/build/buildroot/output')
PORT=Path('/root/lilsmoke-sm64-soft')
PIN='a173bd0a3eeed9b064a234e528f128424d46ea08'
PRIVATE=Path('/root/lilsmoke-sm64-original-private-iig2ryae/build/original-mips')
COMPILER=Path('/root/lilsmoke-public-compiler/runtime')
PYTHON=Path('/root/lilsmoke-public-python/dest/usr')
PREFIXES=('actors/','assets/','bin/','data/','include/','levels/','lib/src/','lib/include/',
    'src/','sound/','text/','textures/','tools/')
ROOT_FILES={'Makefile','Makefile.split','assets.json','charmap.txt','charmap_menu.txt',
    'extract_assets.py','sm64.us.sha1','undefined_syms.txt'}
FORBIDDEN_SUFFIXES={'.png','.z64','.n64','.v64','.aiff','.aifc','.m64','.bin','.o','.so','.exe'}


def sha(path):
    digest=hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda:source.read(1024*1024),b''): digest.update(block)
    return digest.hexdigest()


def run(command,**kwargs):
    subprocess.run(list(map(str,command)),check=True,**kwargs)


def copy(source,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,destination)
    destination.chmod(0o755 if source.stat().st_mode&0o111 else 0o644)


def clean_source(folder):
    archive=subprocess.check_output(['git','-C',str(PORT),'archive',PIN])
    selected=[]
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for item in source:
            if not item.isfile(): continue
            name=item.name
            if name not in ROOT_FILES and not name.startswith(PREFIXES): continue
            if name.startswith(('tools/ido5.3_compiler/','src/pc/gfx/dxsdk/')): continue
            if Path(name).suffix.lower() in FORBIDDEN_SUFFIXES: continue
            target=folder/name
            target.parent.mkdir(parents=True,exist_ok=True)
            data=source.extractfile(item).read()
            target.write_bytes(data)
            target.chmod(item.mode&0o777)
            selected.append(name)
    assert not any((folder/name).suffix.lower() in FORBIDDEN_SUFFIXES for name in selected)
    graphics=folder/'src/pc/gfx/LICENSE.txt'
    assert 'Redistributions in binary form are not allowed.' in graphics.read_text()
    for patch in ('0005-rgb565-header.patch','0001-full-frame-scissor.patch','0003-mips-raster.patch',
        '0006-span-step.patch','0007-depth-first-spans.patch','0008-endpoint-reuse.patch',
        '0009-block-occlusion.patch',
        '0002-hud-raster-size.patch','0004-sampler-cache.patch'):
        run(['patch','-p1','--fuzz=0','--batch','-i',P/'tools/portsmoke/sm64-original'/patch],cwd=folder,
            stdout=subprocess.DEVNULL)
    adaptation=folder/'lilsmoke'
    adaptation.mkdir()
    for path in (P/'package/lilsmoke-sm64-original/src').iterdir():
        if path.suffix in ('.c','.h','.cpp'): copy(path,adaptation/path.name)
    for name in ('m8-audio.c','m8-audio.h'):
        copy(P/'board/segam-m8-v6-stock'/name,adaptation/name)
    for name in ('m8_pocketsnes_audio.c','m8_pocketsnes_audio.h'):
        copy(P/'package/opendingux-emulators'/name,adaptation/name)
    copy(HERE/'public.mk',adaptation/'public.mk')
    copy(P/'LICENSES/MPL-2.0.txt',adaptation/'LICENSE')
    # All helper programs are pure tools, not the restricted graphics/game binary.
    with (folder.parent/'tools-build.log').open('w') as log:
        run(['make','-j1','-C',folder/'tools',
            'CC='+str(SDK/'host/bin/mipsel-buildroot-linux-musl-gcc'),
            'CXX='+str(SDK/'host/bin/mipsel-buildroot-linux-musl-g++')],stdout=log,stderr=subprocess.STDOUT)
    return selected


def stage_compiler(runtime):
    compiler=runtime/'compiler'
    wanted=['lib/mipsel-linux-gnu/ld.so.1',
        'usr/bin/mipsel-linux-gnu-gcc-12','usr/bin/mipsel-linux-gnu-cpp-12',
        'usr/lib/gcc/mipsel-linux-gnu/12/cc1','usr/lib/gcc/mipsel-linux-gnu/12/collect2',
        'usr/lib/gcc/mipsel-linux-gnu/12/liblto_plugin.so',
        'usr/bin/mipsel-linux-gnu-as','usr/bin/mipsel-linux-gnu-ld',
        'usr/bin/mipsel-linux-gnu-objcopy','usr/bin/make']
    wanted += [p.relative_to(COMPILER).as_posix() for base in ('lib/mipsel-linux-gnu','usr/lib/mipsel-linux-gnu')
        for p in (COMPILER/base).glob('*.so*') if p.is_file()]
    wanted += [p.relative_to(COMPILER).as_posix() for p in (COMPILER/'usr/lib/gcc/mipsel-linux-gnu/12/include').rglob('*') if p.is_file()]
    wanted += [p.relative_to(COMPILER).as_posix() for p in (COMPILER/'usr/share/doc').rglob('*') if p.is_file() and p.name in ('copyright','changelog.Debian.gz')]
    for name in sorted(set(wanted)): copy(COMPILER/name,compiler/name)
    for path in (COMPILER/'wrappers').iterdir(): copy(path,compiler/'wrappers'/path.name)


def stage_python(runtime):
    root=runtime/'python/usr'
    copy(PYTHON/'bin/python3.11',root/'bin/python3.11')
    library=PYTHON/'lib/python3.11'
    for path in library.rglob('*'):
        if not path.is_file(): continue
        relative=path.relative_to(library)
        if any(part in ('__pycache__','test','tests','idlelib','tkinter','ensurepip','site-packages')
            or part.startswith('config-') for part in relative.parts): continue
        if path.suffix=='.pyc': continue
        copy(path,root/'lib/python3.11'/relative)
    copy(Path('/root/lilsmoke-public-python/source/LICENSE'),runtime/'licenses/Python.txt')
    launcher=runtime/'bin/python3'
    launcher.parent.mkdir(parents=True,exist_ok=True)
    launcher.write_text('#!/bin/sh\nexec "${LILSMOKE_APP_DIR:?}/runtime/python/usr/bin/python3.11" "$@"\n')
    launcher.chmod(0o755)


def stage_libraries(runtime):
    readelf=SDK/'host/bin/mipsel-buildroot-linux-musl-readelf'
    pending=[Path('/root/lilsmoke-public-bootstrap/sm64-prepare'),PRIVATE/'sm64.us.f3dex2e']
    done=set()
    while pending:
        binary=pending.pop()
        dynamic=subprocess.check_output([str(readelf),'-d',str(binary)],text=True)
        for name in re.findall(r'\(NEEDED\).*\[([^]]+)\]',dynamic):
            if name=='libc.so' or name in done: continue
            location=next((base/name for base in (SDK/'target/usr/lib',SDK/'target/lib') if (base/name).is_file()),None)
            if location is None: raise ValueError('Missing bundled runtime '+name)
            copy(location,runtime/'lib'/name)
            done.add(name)
            pending.append(location)
    copy(SDK/'target/usr/lib/libusb-1.0.so.0',runtime/'lib/libusb-1.0.so.0')
    return sorted(done)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--refresh-source',action='store_true',
        help='Refresh source and preparation code in an existing development candidate')
    args=parser.parse_args()
    app=args.output
    app.mkdir(parents=True,exist_ok=args.refresh_source)
    runtime=app/'runtime'
    for path in (HERE/'app').rglob('*'):
        if path.is_file(): copy(path,app/path.relative_to(HERE/'app'))
    copy(P/'package/portsmoke-sm64/src/icon.webp',app/'icon.webp')
    copy(P/'LICENSES/MPL-2.0.txt',app/'LICENSE')
    copy(Path('/root/lilsmoke-public-bootstrap/sm64-prepare'),runtime/'bin/sm64-prepare')
    copy(HERE/'cook.py',runtime/'cook.py')
    copy(HERE/'native_build.py',runtime/'native_build.py')
    with tempfile.TemporaryDirectory(prefix='sm64-public-source-',dir='/root') as temporary:
        source=Path(temporary)/'source'
        source.mkdir()
        selected=clean_source(source)
        with tarfile.open(runtime/'source.tar.gz','w:gz') as archive:
            for path in sorted(source.rglob('*')):
                if path.is_file(): archive.add(path,arcname=path.relative_to(source).as_posix(),recursive=False)
    if args.refresh_source:
        if not audit(app,SDK,install=True)['passed']:
            raise ValueError('Bundled runtime dependency closure is incomplete')
        recipe_path=runtime/'recipe.json'
        recipe=json.loads(recipe_path.read_text())
        recipe['source_sha256']=sha(runtime/'source.tar.gz')
        recipe['candidate_requires_end_to_end_acceptance']=True
        recipe_path.write_text(json.dumps(recipe,indent=2)+'\n')
        print(json.dumps(dict(source_files=len(selected),source_refreshed=True,public_package_ready=False)))
        return
    stage_compiler(runtime)
    stage_python(runtime)
    libraries=stage_libraries(runtime)
    for path in (SDK/'staging/usr/include').rglob('*'):
        relative=path.relative_to(SDK/'staging/usr/include')
        if len(relative.parts)>1 and relative.parts[0]=='linux' and relative.parts[1] in ('netfilter','netfilter_ipv4','netfilter_ipv6'):
            continue
        if path.is_file(): copy(path,runtime/'sysroot/usr/include'/relative)
    for name in ('crt1.o','crti.o','crtn.o','libc.so','libm.a','libpthread.a','libdl.a'):
        location=SDK/'staging/lib'/name
        if not location.is_file(): location=SDK/'staging/usr/lib'/name
        copy(location,runtime/'sysroot/lib'/name)
    for name in ('crtbegin.o','crtend.o','libgcc.a'):
        location=Path(subprocess.check_output([str(SDK/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-print-file-name='+name],text=True).strip())
        copy(location,runtime/'sysroot/gcc'/name)
    for name in ('libSDL2.so','libstdc++.so','libgcc_s.so'):
        location=SDK/'staging/usr/lib'/name
        if not location.is_file(): location=SDK/'staging/lib'/name
        copy(location,runtime/'sysroot/usr/lib'/name)
    copy(runtime/'sysroot/gcc/libgcc.a',runtime/'sysroot/usr/lib/libgcc.a')
    for name in ('lilsmoke-input.o','lilsmoke-common-input.o','lilsmoke-keymap.o','lilsmoke-log.o'):
        copy(PRIVATE/name,runtime/'objects'/name)
    closure=audit(app,SDK,install=True)
    if not closure['passed']:
        raise ValueError('Bundled runtime dependency closure is incomplete')
    libraries=sorted(set(libraries)|set(closure['dependencies']))
    recipe=dict(schema=1,version='Pre-Alpha-0.0.1',source_pin=PIN,
        source_sha256=sha(runtime/'source.tar.gz'),compile_jobs=308,
        contains_user_rom_assets=False,renderer_binary_distributed=False,
        candidate_requires_end_to_end_acceptance=True,license_source_materials_complete=False,
        bundled_libraries=libraries,clean_upstream_source_files=len(selected))
    (runtime/'recipe.json').write_text(json.dumps(recipe,indent=2)+'\n')
    print(json.dumps(dict(candidate=str(app),source_files=len(selected),
        bytes=sum(p.stat().st_size for p in app.rglob('*') if p.is_file()),
        public_package_ready=False),indent=2))


if __name__=='__main__': main()
