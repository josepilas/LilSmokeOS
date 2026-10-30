#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Attach notices and exact source material; generated game caches are excluded."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

HERE=Path(__file__).resolve().parent
P=HERE.parents[2]
SDK=Path('/root/lilsmoke-os/build/buildroot/output')


def copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,target)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,required=True)
    args=parser.parse_args()
    runtime=args.app/'runtime'
    materials=runtime/'corresponding-source'
    materials.mkdir(exist_ok=True)
    debian=Path('/root/lilsmoke-public-compiler/corresponding-source')
    source_index=json.loads((debian/'SOURCE-INDEX.json').read_text())
    for name,row in source_index['files'].items():
        source=debian/name
        assert hashlib.sha256(source.read_bytes()).hexdigest()==row['sha256']
        copy(source,materials/'debian'/name)
    copy(debian/'SOURCE-INDEX.json',materials/'debian/SOURCE-INDEX.json')
    for package,name in (('gcc','gcc-12.3.0.tar.xz'),('musl','musl-1.2.4.tar.gz'),
        ('python3','Python-3.11.8.tar.xz'),('sdl2','SDL2-2.28.5.tar.gz'),
        ('webp','libwebp-1.3.2.tar.gz'),('json-c','json-c-0.17.tar.gz'),
        ('libzlib','zlib-1.3.1.tar.xz'),('libffi','libffi-3.4.4.tar.gz'),
        ('bzip2','bzip2-1.0.8.tar.gz'),('xz','xz-5.4.5.tar.bz2'),
        ('e2fsprogs','e2fsprogs-1.47.0.tar.xz'),('ncurses','ncurses-6.4-20230603.tar.gz')):
        source=SDK.parent/'dl'/package/name
        if not source.is_file():
            choices=list((SDK.parent/'dl'/package).glob('*.tar.*'))
            assert len(choices)==1,package
            source=choices[0]
        copy(source,materials/'upstream'/source.name)
    copy(Path('/root/lilsmoke-hidapi-20261011/libusb-1.0.26.tar.bz2'),materials/'upstream/libusb-1.0.26.tar.bz2')
    for source in (P/'package/lilsmoke-usb-gamepads/portable-usb').iterdir():
        if source.is_file(): copy(source,materials/'libusb-portable'/source.name)
    input_dir=materials/'lilsmoke-input'
    for name in ('Input.cpp','Input.h','Keymap.cpp','Keymap.h','Log.cpp','Log.h','gamepad-state.h'):
        copy(P/'package/lilsmoke-ui/src/src'/name,input_dir/name)
    copy(P/'package/lilsmoke-od-sdl/src/input-wire.h',input_dir/'input-wire.h')
    copy(P/'LICENSES/MPL-2.0.txt',input_dir/'LICENSE')
    notices={
        'MPL-2.0.txt':P/'LICENSES/MPL-2.0.txt',
        'SDL2.txt':SDK/'build/sdl2-2.28.5/LICENSE.txt',
        'WebP.txt':SDK/'build/webp-1.3.2/COPYING',
        'json-c.txt':SDK/'build/json-c-0.17/COPYING',
        'libusb.txt':Path('/root/lilsmoke-hidapi-20261011/libusb-1.0.26/COPYING'),
        'musl.txt':SDK/'build/musl-1.2.4/COPYRIGHT',
        'GCC-GPLv3.txt':SDK/'build/gcc-final-12.3.0/COPYING3',
        'GCC-Runtime-Exception.txt':SDK/'build/gcc-final-12.3.0/COPYING.RUNTIME',
        'libstdc++-GPLv3.txt':SDK/'build/gcc-final-12.3.0/COPYING3',
        'zlib.txt':SDK/'build/libzlib-1.3.1/LICENSE',
        'libffi.txt':SDK/'build/libffi-3.4.4/LICENSE',
        'bzip2.txt':SDK/'build/bzip2-1.0.8/LICENSE',
        'xz.txt':SDK/'build/xz-5.4.5/COPYING',
        'libuuid-BSD.txt':SDK/'build/e2fsprogs-1.47.0/lib/uuid/COPYING',
        'ncurses.txt':SDK/'build/ncurses-6.4-20230603/COPYING',
        'sm64-base-CC0.txt':Path('/root/lilsmoke-sm64-base/LICENSE.md')}
    for name,source in notices.items(): copy(source,runtime/'licenses'/name)
    (runtime/'licenses/NOTICE.txt').write_text(
        'SM64-LilSmoke original adaptations: Copyright 2026 Jose Pilas, MPL-2.0.\n'
        'Original game engine lineage: n64decomp/sm64 and the pinned DrUm78 software port.\n'
        'The upstream graphics code is distributed as licensed source only.\n'
        'Its binary is generated locally from source, not included in this download.\n'
        'No ROM or extracted texture/music/sample cache is distributed.\n'
        'Bundled GNU compiler tools and libraries retain their individual GPL/LGPL terms.\n'
        'Compiler dependencies use isolated glibc; Python and the generated game use musl.\n'
        'Matching source archives, Debian packaging and input adaptations are bundled.\n'
        'Source/build configurations and dependency notices are supplied for review.\n')
    build_materials=materials/'build-recipes'
    for path in (HERE/'compiler').iterdir():
        if path.is_file() and path.suffix in ('.c','.sh','.py','.sources'):
            copy(path,build_materials/'compiler'/path.name)
    for name in ('public.mk','native_build.py','cook.py','stage_payload.py','audit_runtime.py'):
        copy(HERE/name,build_materials/name)
    for name in ('build_hidapi.py','build_portable_usb.py'):
        copy(P/'tools/gamepads'/name,build_materials/name)
    buildroot=SDK.parent
    config={line.split('=',1)[0]:line.split('=',1)[1] for line in (buildroot/'.config').read_text().splitlines()
        if line.startswith('BR2_') and '=' in line}
    dependency_config={key:value for key,value in config.items()
        if key.startswith(('BR2_mips','BR2_GCC','BR2_TOOLCHAIN','BR2_OPTIMIZE','BR2_SHARED',
            'BR2_STATIC','BR2_PACKAGE_SDL2','BR2_PACKAGE_WEBP','BR2_PACKAGE_JSON_C',
            'BR2_PACKAGE_LIBFFI','BR2_PACKAGE_BZIP2','BR2_PACKAGE_XZ','BR2_PACKAGE_NCURSES',
            'BR2_PACKAGE_E2FSPROGS','BR2_PACKAGE_LIBZLIB','BR2_PACKAGE_LIBUSB'))}
    (build_materials/'dependency-buildroot-config.json').write_text(json.dumps(dependency_config,indent=2)+'\n')
    configure_commands={}
    for name,folder in (('SDL2',SDK/'build/sdl2-2.28.5'),('WebP',SDK/'build/webp-1.3.2'),
        ('xz',SDK/'build/xz-5.4.5'),('ncurses',SDK/'build/ncurses-6.4-20230603'),
        ('libuuid',SDK/'build/e2fsprogs-1.47.0'),('Python',Path('/root/lilsmoke-public-python/build')),
        ('libusb',Path('/root/lilsmoke-hidapi-20261011/libusb-1.0.26'))):
        result=subprocess.run([str(folder/'config.status'),'--config'],text=True,capture_output=True)
        if result.returncode==0:
            configure_commands[name]=result.stdout.strip()
        else:
            copy(folder/'config.status',build_materials/'configure-status'/name/'config.status')
            copy(folder/'config.log',build_materials/'configure-status'/name/'config.log')
            configure_commands[name]=dict(exact_config_status='configure-status/'+name+'/config.status',
                exact_config_log='configure-status/'+name+'/config.log')
    (build_materials/'configure-commands.json').write_text(json.dumps(configure_commands,indent=2)+'\n')
    for name in ('sdl2','webp','json-c','gcc','musl','libffi','bzip2','xz','e2fsprogs','ncurses','libzlib'):
        source=buildroot/'package'/name
        if source.is_dir():
            for path in source.rglob('*'):
                if path.is_file(): copy(path,build_materials/'buildroot-package-rules'/name/path.relative_to(source))
    for name in ('json-c-0.17','libffi-3.4.4','libzlib-1.3.1','musl-1.2.4','gcc-final-12.3.0'):
        folder=SDK/'build'/name
        for relative in ('CMakeCache.txt','config.mak','build/config.status','build/config.log'):
            path=folder/relative
            if path.is_file(): copy(path,build_materials/'generated-config'/name/relative)
    for path in (P/'package/lilsmoke-sm64-original/src').iterdir():
        if path.is_file(): copy(path,build_materials/'adaptation'/path.name)
    for name in ('m8-audio.c','m8-audio.h'):
        copy(P/'board/segam-m8-v6-stock'/name,build_materials/'adaptation'/name)
    for name in ('m8_pocketsnes_audio.c','m8_pocketsnes_audio.h'):
        copy(P/'package/opendingux-emulators'/name,build_materials/'adaptation'/name)
    for folder in ('python','compiler','lib','objects','sysroot'):
        entries={path.relative_to(runtime).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (runtime/folder).rglob('*') if path.is_file()}
        (materials/(folder+'-sha256.json')).write_text(json.dumps(entries,indent=2)+'\n')
    recipe_path=runtime/'recipe.json'
    recipe=json.loads(recipe_path.read_text())
    recipe['corresponding_source_materials_attached']=True
    recipe['license_source_materials_complete']=False
    recipe_path.write_text(json.dumps(recipe,indent=2)+'\n')
    (runtime/'source-material-review.json').write_text(json.dumps(dict(schema=1,
        complete=True,scope='Bundled compiler, helper, Python and library source/notices/build inputs',
        configure_records=list(configure_commands),debian_source_index='corresponding-source/debian/SOURCE-INDEX.json',
        source_only_renderer=True,physical_device_acceptance=False,legal_clearance_asserted=False),indent=2)+'\n')
    print(json.dumps(dict(source_material_bytes=sum(p.stat().st_size for p in materials.rglob('*') if p.is_file()),
        public_package_ready=False,source_build_inputs_review_complete=True,
        legal_clearance_asserted=False),indent=2))


if __name__=='__main__': main()
