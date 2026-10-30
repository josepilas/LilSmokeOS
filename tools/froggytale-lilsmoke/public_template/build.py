#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Offline build of the retained, qualified sources; no game/SDK downloads."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def stage(out):
    pristine = ROOT/'vendor/froggytale'
    inventory = json.loads((pristine/'source-inventory.json').read_text())
    assert inventory['revision'] == '5699418f671dc8bb07dc2e4adc60169b616ab906'
    for item in inventory['files']:
        data = (pristine/item['path']).read_bytes()
        assert len(data) == item['size']
        assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest() == item['sha']
    tree = out/'stage'
    shutil.copytree(pristine, tree)
    loop = tree/'src/loop.c'
    text = loop.read_text()
    old = '        options.parseTxtr = true;'
    assert text.count(old) == 1
    text = text.replace(old, '        options.parseTxtr = false; /* LilSmoke: prepacked BYO atlas, no page decode. */')
    text = text.replace('        if (!args.headless)\n            options.parseAudo = true;',
        '        if (!args.headless || getenv("FROGGY_HEADLESS_AUDIO"))\n            options.parseAudo = true;')
    text = text.replace('        if (args.headless) {\n            audioSystem =',
        '        if (args.headless && !getenv("FROGGY_HEADLESS_AUDIO")) {\n            audioSystem =')
    text = '#include "lilsmoke_audio.h"\n'+text
    cleanup='        runner->audioSystem->vtable->destroy(runner->audioSystem);'
    assert text.count(cleanup)==1
    text=text.replace(cleanup,'        if (!args.headless || getenv("FROGGY_HEADLESS_AUDIO"))\n'
        '            logInfo("UNDERTALE_AUDIO ready=%d frames=%llu nonzero_frames=%llu\\n", LilSmokeAudio_ready(runner->audioSystem), '
        '(unsigned long long)LilSmokeAudio_frames(runner->audioSystem), '
        '(unsigned long long)LilSmokeAudio_nonzeroFrames(runner->audioSystem));\n'+cleanup)
    loop.write_text(text)
    spec = importlib.util.spec_from_file_location('audio_stage', ROOT/'audio/stage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.stage(tree, out/'audio')
    return tree


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--verify-only', action='store_true')
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True, exist_ok=False)
    tree=stage(a.out)
    plan=json.loads((ROOT/'build-plan.json').read_text())
    sources=[]
    for item in plan['sources']:
        name=item['path']
        path=ROOT/'src'/Path(name).name if name.startswith('adapter/') else \
            a.out/'audio'/Path(name).name if name.startswith('audio/') else tree/name
        assert sha(path)==item['sha256'], 'Qualified source differs: '+name
        sources.append((name,path))
    for name,digest in plan['native_headers'].items():
        assert sha(ROOT/'include/native'/name)==digest, name
    for name,digest in plan['audio_outputs'].items():
        assert sha(a.out/'audio'/name)==digest, name
    proof=dict(passed=True, source_units=len(sources), upstream_files=len(
        json.loads((tree/'source-inventory.json').read_text())['files']),
        offline=True, engine_rebuilt=False, package_engine_sha256=plan['release_engine_sha256'])
    if not a.verify_only:
        sdk=Path(os.environ['SDK']).resolve()
        cc=sdk/'host/bin/mipsel-buildroot-linux-musl-gcc'
        includes=[tree/x for x in plan['upstream_include_dirs']]
        includes += [ROOT/'deps/SDL2-build-headers', ROOT/'src', ROOT/'include/native']
        flags=plan['flags']+['-I'+str(x) for x in includes]
        allowed=os.sched_getaffinity(0)
        cpu=int(os.environ.get('FROGGY_CPU',min(allowed)))
        assert cpu in allowed
        prefix=['taskset','-c',str(cpu),'nice','-n','15','prlimit','--as=805306368','--']
        commands=[]
        def run(cmd):
            commands.append(list(map(str,cmd)))
            with (a.out/'build.log').open('a') as log:
                subprocess.run(prefix+list(map(str,cmd)),stdout=log,stderr=subprocess.STDOUT,check=True)
        objects=[]
        for index,(name,path) in enumerate(sources):
            local=flags[:]
            if name in ('src/loop.c','audio/ma_audio_system.c'):
                local+=['-DUSE_MINIAUDIO','-DFROGGY_SDL_AUDIO=1','-I'+str(a.out/'audio'),
                    '-I'+str(ROOT/'audio'),'-I'+str(tree/'src/audio/miniaudio'),'-I'+str(tree/'vendor/miniaudio')]
            if name=='audio/ma_audio_system.c': local+=['-fno-tree-vrp']
            obj=a.out/(str(index)+'.o');run([cc,*local,'-c',path,'-o',obj]);objects.append(obj)
        binary=a.out/'froggytale-lilsmoke'
        run([cc,*flags,'-Wl,--gc-sections',*objects,'-lSDL2','-lm','-ldl','-lpthread','-latomic','-o',binary])
        proof.update(engine_rebuilt=True,binary_sha256=sha(binary),bit_identical_claim=False)
        (a.out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (a.out/'report.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof,indent=2))


if __name__=='__main__':main()
