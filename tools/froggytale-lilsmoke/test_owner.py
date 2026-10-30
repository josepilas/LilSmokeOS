#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Private-only owner WAD qualification. Never copy the original or bundle outputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

HERE=Path(__file__).resolve().parent
SDK=Path(os.environ.get('SDK','/root/lilsmoke-os/build/buildroot/output'))


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda:f.read(1024*1024),b''):h.update(data)
    return h.hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--wad',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--frames',type=int,default=120)
    p.add_argument('--cache',type=Path,help='Reuse the private native-prepared cache, not another copy of game data')
    p.add_argument('--dump-json',action='store_true',help='Exercise upstream diagnostic serialization separately')
    a=p.parse_args();a.wad=a.wad.resolve(strict=True);a.out=a.out.resolve()
    a.out.mkdir(parents=True,exist_ok=False)
    binary=HERE/'_build/mips/froggytale-lilsmoke'
    before=digest(a.wad)
    env=os.environ.copy();env.update(FROGGY_INPUT_DIR='/nonexistent',FROGGY_NO_SHARED_INPUT='1',
        FROGGY_NONINTERACTIVE='1',FROGGY_CACHE_DIR=str(a.cache.resolve() if a.cache else a.out/'cache'),FROGGY_SAVE_DIR=str(a.out/'saves'),
        FROGGY_CAPTURE_DIR=str(a.out))
    env.pop('LD_PRELOAD',None);env.pop('LILSMOKE_HCFB',None);env.pop('SW_ASSET_DIR',None)
    prefix=['taskset','-c',os.environ.get('FROGGY_CPU','1'),'nice','-n','15',
            '/usr/bin/time','-f','RESOURCES elapsed=%e rss_kib=%M','qemu-mipsel','-cpu','74Kf','-L',str(SDK/'staging'),str(binary)]
    report=dict(private_evidence=True,source_bytes=a.wad.stat().st_size,source_sha256=before,
        ogg_files_in_original_directory=len(list(a.wad.parent.glob('*.ogg'))),binary_sha256=digest(binary),
        audio_supported=False,physical_hardware_tested=False,results=[])
    def run(name,args,limit=900):
        start=time.monotonic();log=a.out/(name+'.log')
        with log.open('w') as output:
            proc=subprocess.Popen(prefix+list(map(str,args)),env=env,cwd=a.out,stdout=output,stderr=subprocess.STDOUT)
            try:code=proc.wait(timeout=limit)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:proc.kill();proc.wait()
                raise
        result=dict(name=name,exit_code=code,elapsed_seconds=time.monotonic()-start)
        report['results'].append(result);print(json.dumps(result),flush=True)
        text=log.read_text(errors='replace')
        if code:print(text[-5000:],flush=True)
        assert code==0,(name,code)
        return text
    try:
        text=run('first-preparation',['--prepare-only',a.wad,'--headless'])
        assert ('cache=hit' if a.cache else 'cache=created') in text
        caches=list(Path(env['FROGGY_CACHE_DIR']).glob('*-v1'))
        assert len(caches)==1
        atlas=caches[0]
        report['atlas']={n:dict(bytes=(atlas/n).stat().st_size,sha256=digest(atlas/n)) for n in ('ATLAS.BIN','ATLAS.RGBA4444')}
        text=run('cached-preparation',['--prepare-only',a.wad,'--headless'])
        assert 'cache=hit' in text
        options=[a.wad,'--headless','--exit-at-frame',a.frames,'--trace-frames']
        if a.dump_json:options+=['--dump-frame-json',str(a.frames),'--dump-frame-json-file',str(a.out/'state-%d.json')]
        text=run('actual-owner-runtime',options)
        assert 'FROGGY_EXIT status=0' in text
        report['runtime_reached_frame']=a.frames
        report['passed']=True
    finally:
        report['original_unchanged']=a.wad.exists() and digest(a.wad)==before
        (a.out/'PRIVATE-report.json').write_text(json.dumps(report,indent=2)+'\n')
        print('PRIVATE evidence:',a.out,flush=True)


if __name__=='__main__':main()
