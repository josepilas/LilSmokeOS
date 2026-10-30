#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Stage a bounded source-only public checkout. Never rebuild, publish or access SD."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

HERE=Path(__file__).resolve().parent
ENGINE='1a0754e12f32c6a8e72b75c4f5968e0534037bd7f31e09702e0bafdd11d07561'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,indent=2,ensure_ascii=True)+'\n')


def copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,target)


def source_tree(source,target):
    excluded=[]
    for path in sorted(source.rglob('*')):
        rel=path.relative_to(source)
        if not path.is_file() or any(x in rel.parts for x in ('.git','__pycache__','.libs','.deps','autom4te.cache')):continue
        if path.suffix.lower() in ('.o','.a','.so','.lo','.la','.pyc','.png','.bmp','.jpg','.gif','.ico','.pdf','.wav','.ogg','.zip','.gz','.xz'):
            excluded.append(rel.as_posix());continue
        data=path.read_bytes()
        if b'\0' in data:
            excluded.append(rel.as_posix());continue
        copy(path,target/rel)
    return excluded


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--refresh-templates',action='store_true')
    a=p.parse_args();sdk=a.sdk.resolve()
    root=HERE/'_build/public-0.0.1'
    if a.refresh_templates:
        assert root.is_dir()
    else:root.mkdir(parents=True,exist_ok=False)
    for path in (HERE/'public_template').rglob('*'):
        if path.is_file():copy(path,root/path.relative_to(HERE/'public_template'))
    if a.refresh_templates:
        print('Refreshed authored templates only; runtime/upstream/dependencies untouched')
        return
    previous=HERE/'_build/package'
    assert sha(previous/'app/runtime/bin/froggytale-lilsmoke')==ENGINE
    old_source=previous/'Undertale-LilSmoke-0.1.0-corresponding-source.zip'
    with zipfile.ZipFile(old_source) as archive:
        prefix='tools/froggytale-lilsmoke/'
        for name in archive.namelist():
            rel=name.removeprefix(prefix)
            if name.startswith(prefix) and (rel.startswith('src/') or rel in (
                'audio/stage.py','audio/sdl_pull.inc','audio/lilsmoke_audio.h','fixture.py','upstream.lock.json','app/run.sh')):
                dest=root/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(archive.read(name))
    upstream=HERE/'_build/upstream'
    shutil.copytree(upstream,root/'vendor/froggytale')
    native=HERE/'_build/mips/native'
    for name in ('gamepad-state.h','font8x8_basic.h'):copy(native/name,root/'include/native'/name)
    sdl=sdk/'build/sdl2-2.28.5'
    excluded={}
    for name in ('src','include','cmake','acinclude','build-scripts'):
        excluded['SDL2/'+name]=source_tree(sdl/name,root/'deps/SDL2'/name)
    for path in sdl.iterdir():
        if path.is_file() and (path.suffix.lower() in ('.txt','.md','.in','.ac','.sh','.m4','.cmake') or
            path.name in ('configure','Makefile.minimal','Makefile.rules','CMakeLists.txt','autogen.sh')):
            if path.name not in ('config.log','config.status','Makefile.rules'):copy(path,root/'deps/SDL2'/path.name)
    source_tree(sdk/'staging/usr/include/SDL2',root/'deps/SDL2-build-headers')
    gcc=sdk/'build/host-gcc-final-12.3.0';gdest=root/'deps/gcc-12.3.0'
    for name in ('libatomic','config','include'):excluded['GCC/'+name]=source_tree(gcc/name,gdest/name)
    for path in gcc.iterdir():
        if path.is_file() and (path.suffix in ('.m4','.am') or path.name in (
            'config-ml.in','config.guess','config.sub','config.rpath','install-sh','ltmain.sh',
            'depcomp','missing','mkinstalldirs','move-if-change','ylwrap','test-driver','COPYING','COPYING3','COPYING.LIB','COPYING.RUNTIME')):
            copy(path,gdest/path.name)
    for name in ('BASE-VER','DATESTAMP','DEV-PHASE'):
        if (gcc/'gcc'/name).is_file():copy(gcc/'gcc'/name,gdest/'gcc'/name)
    copy(gcc/'contrib/make_sunver.pl',gdest/'contrib/make_sunver.pl')
    copy(upstream/'LICENSE',root/'LICENSE');copy(upstream/'LICENSE',root/'LICENSES/AGPL-3.0-only.txt')
    copy(HERE.parents[1]/'LICENSES/MPL-2.0.txt',root/'LICENSES/MPL-2.0.txt')
    copy(sdl/'LICENSE.txt',root/'LICENSES/SDL2-zlib.txt')
    copy(gcc/'COPYING3',root/'LICENSES/GCC-GPL-3.0.txt')
    copy(gcc/'COPYING.RUNTIME',root/'LICENSES/GCC-Runtime-Exception.txt')
    build=json.loads((HERE/'_build/mips/build.json').read_text())
    plan=dict(flags=[x for x in build['flags'] if not x.startswith('-I')],sources=build['sources'],
        upstream_include_dirs=['src','src/image','src/video','src/audio','src/debug_font','src/sw_renderer',
            'vendor/stb/ds','vendor/stb/image','vendor/stb/vorbis','vendor/bzip2','vendor/md5','vendor/miniz','vendor/sha1','vendor/base64'],
        native_headers={name:sha(native/name) for name in ('gamepad-state.h','font8x8_basic.h')},
        audio_outputs={name:sha(HERE/'_build/mips/audio'/name) for name in ('ma_audio_system.c','ma_audio_system.h')},
        release_engine_sha256=ENGINE,qualified_unstripped_sha256=build['binary_sha256'])
    write_json(root/'build-plan.json',plan)
    lock=dict(public_version='0.0.1',internal_engine_version='0.1.0',
        files={p.relative_to(previous/'app/runtime').as_posix():sha(p)
            for p in sorted((previous/'app/runtime').rglob('*')) if p.is_file()})
    write_json(root/'release-lock.json',lock)
    meta=json.loads((HERE/'app/meta.json').read_text())
    meta.update(version='0.0.1',author='Jos\u00e9 Pilas; FroggyTale / Butterscotch contributors')
    meta['author']=meta['author'].replace('Jos\u00e9','José')
    write_json(root/'app/meta.json',meta)
    readme=(HERE/'app/README.txt').read_text().replace('Undertale-LilSmoke 0.1.0','Undertale-LilSmoke public 0.0.1')
    (root/'app/README.txt').write_text(readme+'\nThe unchanged qualified engine still displays internal version 0.1.0.\nLilSmoke adaptation: Jose Pilas. See NOTICE.md in the same-release sources.\n')
    vm=json.loads((HERE/'_build/vm-audio-fat32-02/PRIVATE-report.json').read_text())
    qa=json.loads((HERE/'_build/qa-packaged-fat32-final/report.json').read_text())
    assert vm['passed'] and vm['binary_sha256']==build['binary_sha256'] and qa['passed'] and qa['binary_sha256']==ENGINE
    keys=['passed','ram_mib','app_address_limit_mib','cpu','cpus','gpu_3d','physical_hardware',
        'cache_filesystem','cache_remount_verified','audio_requested','audio_frames','nonzero_pcm_frames',
        'owner_original_unchanged','owner_wad_and_oggs_unchanged','sd_touched','cases']
    public={k:vm[k] for k in keys}
    public.update(binary_sha256=ENGINE,qualified_unstripped_sha256=build['binary_sha256'],
        evidence_reused=True,new_gameplay_test_claimed=False,
        synthetic=dict(passed=qa['passed'],cases=qa['results']),
        previous_package_sha256=sha(previous/'Undertale-LilSmoke-0.1.0.lilpck'))
    write_json(root/'evidence/qualification.json',public)
    # Retain configure switches, not host paths, environment dumps or config.log.
    flags=[]
    for line in (sdl/'config.log').read_text(errors='replace').splitlines():
        if line.lstrip().startswith('$ '):
            import shlex
            flags=[x for x in shlex.split(line.strip()[2:]) if x.startswith(('--enable-','--disable-'))]
            break
    write_json(root/'evidence/dependency-build.json',dict(SDL2_version='2.28.5',GCC_version='12.3.0',
        SDL2_configure_switches=flags,generated_SDL_config_sha256=sha(root/'deps/SDL2-build-headers/SDL_config.h'),
        omitted_non_source_dependency_files=excluded,no_SDK_copied=True))
    # Existing engine tests stay AGPL. Public copies use recipient-supplied SDK paths.
    test=(HERE/'test.py').read_text().replace('/root/lilsmoke-os/build/buildroot/output','/path/to/sdk/output')
    (root/'test.py').write_text(test)
    print(json.dumps(dict(public_checkout=str(root),runtime_unchanged=True,engine_sha256=ENGINE,
        source_files=sum(p.is_file() for p in root.rglob('*'))),indent=2))


if __name__=='__main__':main()
