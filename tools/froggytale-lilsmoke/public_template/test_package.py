#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Actual MIPS LilApps inspect/install/upgrade preservation, using synthetic sentinels."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package',type=Path,required=True)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.package=a.package.resolve();a.source=a.source.resolve();a.out=a.out.resolve()
    a.out.mkdir(parents=True,exist_ok=False)
    sdk=Path(os.environ['SDK']);root=a.out/'independent-apps'
    cpu=int(os.environ.get('FROGGY_CPU',min(os.sched_getaffinity(0))))
    command=['taskset','-c',str(cpu),'nice','-n','15','qemu-mipsel','-cpu','74Kf','-L',str(sdk/'staging'),
        '-E','LILSMOKE_APPS_ROOT='+str(root),str(sdk/'target/usr/lib/lilsmoke/lilsmoke-apps')]
    report=dict(passed=False,package_sha256=sha(a.package),source_sha256=sha(a.source),
        operations=[],sd_touched=False,owner_data_used=False,physical_hardware_tested=False)
    def run(name,args,ok=True):
        result=subprocess.run(command+list(map(str,args)),capture_output=True,text=True,timeout=120)
        (a.out/(name+'.log')).write_text(result.stdout+result.stderr)
        report['operations'].append(dict(name=name,exit_code=result.returncode))
        assert (result.returncode==0)==ok,(name,result.stdout,result.stderr)
        return [json.loads(x) for x in result.stdout.splitlines() if x.startswith('{')]
    try:
        with zipfile.ZipFile(a.source) as z:
            assert all('/_build/' not in n and not n.startswith(('_build/','.git/')) for n in z.namelist())
            for name in ('build.py','src/prepare.c','audio/sdl_pull.inc','vendor/froggytale/source-inventory.json',
                'include/native/gamepad-state.h','include/native/font8x8_basic.h','deps/SDL2/src/SDL.c',
                'deps/SDL2-build-headers/SDL_config.h','deps/gcc-12.3.0/libatomic/cas_n.c',
                'LICENSE','NOTICE.md','SOURCE-MANIFEST.json','tools/audit.py','tools/release.py'):
                assert name in z.namelist(),name
        with zipfile.ZipFile(a.package) as z:
            names=z.namelist();meta=json.loads(z.read('meta.json'))
            assert meta['version']=='0.0.1' and meta['license']=='AGPL-3.0-only'
            assert [n for n in names if '/' not in n and n.endswith('.sh')]==['run.sh']
            assert not any(n.startswith(('original/','data/','cache/','saves/')) or
                n.lower().endswith(('.win','.unx','.ios','.ogg','.rgba4444','.png','.webp','.zip')) for n in names)
            offer=z.read('SOURCE-OFFER.txt').decode()
            assert sha(a.source) in offer and '/releases/download/0.0.1/'+a.source.name in offer
            original=hashlib.sha256(z.read('runtime/bin/froggytale-lilsmoke')).hexdigest()
            assert original=='1a0754e12f32c6a8e72b75c4f5968e0534037bd7f31e09702e0bafdd11d07561'
            assert len([n for n in names if 'flowey' in n.lower()])==0
        inspected=run('inspect',['inspect',a.package,a.out/'preview'])[-1]
        assert inspected['id']==meta['id'] and inspected['version']=='0.0.1'
        run('install',['install',a.package,inspected['sha256']])
        app=root/meta['name'];assert sha(app/'runtime/bin/froggytale-lilsmoke')==original
        fixtures={'original/data.win':b'not a game: installer preservation sentinel',
            'original/music.ogg':b'not audio: installer preservation sentinel',
            'cache/synthetic.fixture':b'not an atlas: cache sentinel','saves/synthetic.sav':b'save sentinel'}
        for name,data in fixtures.items():
            path=app/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        run('catalog',['list'])
        update=a.out/'upgrade-fixture.lilpck'
        with zipfile.ZipFile(a.package) as src,zipfile.ZipFile(update,'w',zipfile.ZIP_DEFLATED) as dst:
            for info in src.infolist():
                data=src.read(info.filename)
                if info.filename=='meta.json':data=(json.dumps(dict(meta,version='0.0.2'))+'\n').encode()
                dst.writestr(info,data)
        upgraded=run('inspect-upgrade',['inspect',update,a.out/'preview-upgrade'])[-1]
        run('upgrade',['install',update,upgraded['sha256']])
        assert json.loads((app/'meta.json').read_text())['version']=='0.0.2'
        assert sha(app/'runtime/bin/froggytale-lilsmoke')==original
        assert all((app/name).read_bytes()==data for name,data in fixtures.items())
        run('duplicate-rejected',['install',update],ok=False)
        run('uninstall',['uninstall',meta['name']]);assert not app.exists()
        report.update(passed=True,binary_sha256=original,private_fixture_preserved_on_upgrade=True,
                      package_and_source_asset_free=True,release_tag='0.0.1')
    finally:
        (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))


if __name__=='__main__':main()
