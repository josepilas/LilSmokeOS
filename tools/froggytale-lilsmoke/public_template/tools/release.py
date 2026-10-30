#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas and contributors.
"""Independent exact-byte public release archiver. Never rebuild, install or publish."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
from audit import scan,files,MANIFEST


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def archive(path,entries):
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,data in sorted(entries):
            item=zipfile.ZipInfo(name,(2026,10,28,0,0,0));item.compress_type=zipfile.ZIP_DEFLATED
            executable=name.endswith('.sh') or Path(name).name in ('configure','install-sh','missing','depcomp')
            item.external_attr=(0o100755 if executable else 0o100644)<<16
            z.writestr(item,data)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime',type=Path,required=True,help='Qualified runtime directory containing bin/ and lib/')
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[1]
    audit=scan(root);assert (root/MANIFEST).is_file(), 'Freeze source inventory first'
    lock=json.loads((root/'release-lock.json').read_text())
    for name,digest in lock['files'].items():assert sha(a.runtime/name)==digest, 'Runtime changed: '+name
    a.out.mkdir(parents=True,exist_ok=False)
    stem='Undertale-LilSmoke-0.0.1'
    source=a.out/(stem+'-corresponding-source.zip')
    archive(source,[(p.relative_to(root).as_posix(),p.read_bytes()) for p in files(root)]+[(MANIFEST,(root/MANIFEST).read_bytes())])
    app=a.out/'app';shutil.copytree(root/'app',app)
    for name in lock['files']:
        dest=app/'runtime'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(a.runtime/name,dest)
    licenses=app/'licenses';licenses.mkdir()
    for path in (root/'LICENSES').iterdir():
        if path.is_file():shutil.copyfile(path,licenses/path.name)
    shutil.copyfile(root/'LICENSE',licenses/'AGPL-3.0.txt')
    shutil.copyfile(root/'NOTICE.md',licenses/'NOTICE.txt')
    shutil.copyfile(root/'THIRD-PARTY.md',licenses/'THIRD-PARTY.txt')
    qualification=json.loads((root/'evidence/qualification.json').read_text())
    (app/'QUALIFICATION.txt').write_text('Public packaging 0.0.1; unchanged internal engine 0.1.0.\n'
        'Retained prior qualification, NOT a new gameplay or physical test.\n'
        'One-CPU 128 MiB MIPS VM; 112 MiB process limit; FAT32 preparation/remount/cache and SDL dummy audio.\n'
        'No full playthrough, physical DAC/HDMI/latency/performance or save/load gameplay claim.\n'+
        json.dumps(qualification,indent=2)+'\n')
    url='https://github.com/josepilas/Undertale-LilSmoke/releases/download/0.0.1/'+source.name
    (app/'SOURCE-OFFER.txt').write_text('Undertale-LilSmoke public release 0.0.1\n'
        'This modified AGPL-3.0-only runner is supplied without warranty.\n'
        'Complete corresponding source accompanies the binary in the SAME release:\n'+source.name+'\n'+url+'\n'
        'SHA-256: '+sha(source)+'\n'
        'It includes the pinned upstream subset, adaptations, build/staging scripts, SDL2/libatomic source,\n'
        'exact SDL2 headers, MIT native input and public-domain font plus all retained vendor notices.\n'
        'A compatible recipient-supplied compiler/musl SDK is required; see README.md and docs/DEPENDENCIES.md.\n'
        'An upstream URL or mutable repository HEAD is not a substitute for this modified source.\n'
        'The maintainer must upload this exact source archive with the binary before public conveyance.\n'
        'No game data, music, generated atlas, Flowey art, firmware or private OS code is included.\n')
    meta=json.loads((app/'meta.json').read_text());assert meta['version']=='0.0.1' and meta['license']=='AGPL-3.0-only'
    package=a.out/(stem+'.lilpck')
    entries=[(p.relative_to(app).as_posix(),p.read_bytes()) for p in app.rglob('*') if p.is_file()]
    archive(package,entries)
    with zipfile.ZipFile(package) as z:
        assert all(not n.lower().endswith(('.win','.unx','.ogg','.rgba4444','.png','.webp','.zip')) for n in z.namelist())
        assert [n for n in z.namelist() if '/' not in n and n.endswith('.sh')]==['run.sh']
        assert hashlib.sha256(z.read('runtime/bin/froggytale-lilsmoke')).hexdigest()==lock['files']['bin/froggytale-lilsmoke']
        assert source.name in z.read('SOURCE-OFFER.txt').decode() and sha(source) in z.read('SOURCE-OFFER.txt').decode()
    report=dict(passed=True,tag='0.0.1',title='0.0.1',published=False,sd_writes=False,engine_rebuilt=False,
        engine_sha256=lock['files']['bin/froggytale-lilsmoke'],runtime_files=lock['files'],
        artifacts={p.name:dict(bytes=p.stat().st_size,sha256=sha(p)) for p in (package,source)},
        source_manifest_sha256=sha(root/MANIFEST),source_file_count=audit['file_count']+1,
        source_bytes=audit['bytes'],single_MPL_claim=False,private_game_assets=False)
    (a.out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    (a.out/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in (package,source)))
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
