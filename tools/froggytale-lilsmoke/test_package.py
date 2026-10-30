#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Asset-free install/upgrade/preservation checks with the actual MIPS LilApps helper.

Only disposable app roots below --out are used. No SD or owner data is accessed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

HERE=Path(__file__).resolve().parent
SDK=Path(os.environ.get('SDK','/root/lilsmoke-os/build/buildroot/output'))


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package',type=Path,required=True)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.package=a.package.resolve(strict=True);a.source=a.source.resolve(strict=True)
    a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    root=a.out/'independent-apps'
    command=['taskset','-c',os.environ.get('FROGGY_CPU','1'),'nice','-n','15',
        'qemu-mipsel','-cpu','74Kf','-L',str(SDK/'staging'),'-E','LILSMOKE_APPS_ROOT='+str(root),
        str(SDK/'target/usr/lib/lilsmoke/lilsmoke-apps')]
    report=dict(passed=False,sd_touched=False,owner_data_used=False,operations=[],
        package_sha256=sha(a.package),source_sha256=sha(a.source))
    def run(name,args,ok=True):
        result=subprocess.run(command+list(map(str,args)),capture_output=True,text=True,timeout=120)
        (a.out/(name+'.log')).write_text(result.stdout+result.stderr)
        report['operations'].append(dict(name=name,exit_code=result.returncode))
        assert (result.returncode==0)==ok,(name,result.stdout,result.stderr)
        return [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
    try:
        with zipfile.ZipFile(a.package) as archive:
            names=archive.namelist()
            assert [n for n in names if '/' not in n and n.endswith('.sh')]==['run.sh']
            assert all(not n.lower().endswith(('.win','.unx','.ios','.ogg','.wav','.pcm','.raw','.rgba4444','.exe','.dll')) for n in names)
            assert all(not n.startswith(('original/','cache/','saves/','data/')) for n in names)
            for name in ('SOURCE-OFFER.txt','QUALIFICATION.txt','runtime/lib/libatomic.so.1',
                'licenses/GCC-GPL-3.0.txt','licenses/GCC-Runtime-Exception.txt'):
                assert name in names,name
            assert sha(a.source) in archive.read('SOURCE-OFFER.txt').decode()
            meta=json.loads(archive.read('meta.json'))
            original_binary=hashlib.sha256(archive.read('runtime/bin/froggytale-lilsmoke')).hexdigest()
        with zipfile.ZipFile(a.source) as archive:
            names=archive.namelist()
            assert not any('/_build/' in n and '/_build/upstream/' not in n for n in names)
            assert not any(n.lower().endswith(('.win','.unx','.ogg','.wav','.pcm','.raw','.rgba4444')) for n in names)
            for name in ('tools/froggytale-lilsmoke/test_vm.py','tools/froggytale-lilsmoke/test_package.py',
                'tools/froggytale-lilsmoke/audio/sdl_pull.inc','tools/hwtest/mips_disk_fixture.py'):
                assert name in names,name
        inspected=run('inspect',['inspect',a.package,a.out/'preview'])[-1]
        assert inspected['id']==meta['id'] and inspected['version']==meta['version']
        run('install',['install',a.package,inspected['sha256']])
        app=root/meta['name']
        assert sha(app/'runtime/bin/froggytale-lilsmoke')==original_binary
        assert [p.name for p in app.glob('*.sh')]==['run.sh']
        # Deliberately invalid synthetic bytes, never an owner's game or saves.
        fixtures={'original/data.win':b'not a game: installer preservation fixture',
            'original/music.ogg':b'not audio: installer preservation fixture',
            'cache/atlas.fixture':b'synthetic cache sentinel','saves/test.sav':b'synthetic save sentinel'}
        for name,data in fixtures.items():
            path=app/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        run('catalog',['list'])
        update=a.out/'upgrade-fixture.lilpck'
        with zipfile.ZipFile(a.package) as source,zipfile.ZipFile(update,'w',zipfile.ZIP_DEFLATED) as target:
            for info in source.infolist():
                data=source.read(info.filename)
                if info.filename=='meta.json':
                    updated=dict(meta,version='0.1.1')
                    data=(json.dumps(updated,indent=2)+'\n').encode()
                target.writestr(info,data)
        upgrade=run('inspect-upgrade',['inspect',update,a.out/'preview-upgrade'])[-1]
        run('upgrade',['install',update,upgrade['sha256']])
        assert json.loads((app/'meta.json').read_text())['version']=='0.1.1'
        assert [p.name for p in app.glob('*.sh')]==['run.sh']
        assert sha(app/'runtime/bin/froggytale-lilsmoke')==original_binary
        for name,data in fixtures.items():assert (app/name).read_bytes()==data,name
        run('duplicate-rejected',['install',update],ok=False)
        run('uninstall',['uninstall',meta['name']])
        assert not app.exists()
        report.update(passed=True,private_fixture_preserved_on_upgrade=True,one_launcher=True,
            package_and_source_asset_free=True,physical_hardware_tested=False)
    finally:
        (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':main()
