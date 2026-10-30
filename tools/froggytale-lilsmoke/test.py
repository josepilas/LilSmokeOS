#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Serial real MIPS/musl execution, original synthetic data only, bounded runs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from fixture import make

HERE = Path(__file__).resolve().parent
SDK = Path(os.environ.get('SDK', '/root/lilsmoke-os/build/buildroot/output'))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=HERE/'_build/qa-final')
    p.add_argument('--binary',type=Path,default=HERE/'_build/mips/froggytale-lilsmoke')
    a=p.parse_args();a.out=a.out.resolve();a.binary=a.binary.resolve()
    a.out.mkdir(parents=True,exist_ok=False)
    fixture=make(a.out/'synthetic')
    transport_dir=tempfile.TemporaryDirectory(prefix='froggytale-transport-')
    env=os.environ.copy();env.update(FROGGY_INPUT_DIR='/nonexistent',FROGGY_NO_SHARED_INPUT='1',
        FROGGY_NONINTERACTIVE='1',FROGGY_TEST_DIR=transport_dir.name,FROGGY_SAVE_DIR=str(a.out/'saves'),
        FROGGY_CACHE_DIR=str(a.out/'cache'),SW_ASSET_DIR=str(fixture),FROGGY_TEST_ATLAS='1')
    env.pop('LD_PRELOAD',None);env.pop('LILSMOKE_HCFB',None)
    prefix=['taskset','-c',os.environ.get('FROGGY_CPU','1'),'nice','-n','15',
            'qemu-mipsel','-cpu','74Kf','-L',str(SDK/'staging'),str(a.binary)]
    results=[]
    def run(name,args,expected=0):
        result=subprocess.run(prefix+list(map(str,args)),env=env,cwd=a.out,
            stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=90)
        (a.out/(name+'.log')).write_text(result.stdout)
        print(name,result.returncode,result.stdout[-1400:])
        results.append(dict(name=name,exit_code=result.returncode,expected=expected))
        assert result.returncode==expected,(name,result.stdout)
        return result.stdout
    report=dict(binary=str(a.binary),binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),
        architecture='actual MIPS32r2 74Kf musl under qemu-user; one host CPU',results=results,
        actual_undertale_tested=False,physical_hardware_tested=False)
    try:
        run('version',['--version'])
        text=run('selftest',['--self-test',a.out/'menu.ppm'])
        assert 'failed=0' in text and 'vm_menu_cursor=1 vm_confirms=1' in text
        run('check-synthetic',['--check-data',fixture/'game.unx'])
        text=run('prepare-first',['--prepare-only',fixture/'game.unx','--headless'])
        assert 'cache=created' in text
        generation=next((a.out/'cache').glob('*-v1'))
        before={p.name:(p.stat().st_mtime_ns,hashlib.sha256(p.read_bytes()).hexdigest()) for p in generation.iterdir()}
        text=run('prepare-cached',['--prepare-only',fixture/'game.unx','--headless'])
        assert 'cache=hit' in text
        assert before=={p.name:(p.stat().st_mtime_ns,hashlib.sha256(p.read_bytes()).hexdigest()) for p in generation.iterdir()}
        # DrvFS utime truncates nanoseconds. Seal representable timestamps first
        # so this test changes identity, not the metadata it claims to preserve.
        remounted=(fixture/'game.unx',generation/'ATLAS.RGBA4444')
        for path in remounted:
            stamp=path.stat();mtime=stamp.st_mtime_ns//1000000000*1000000000
            os.utime(path,ns=(stamp.st_atime_ns,mtime))
        env['UNDERTALE_VERIFY_CACHE']='1'
        try:
            text=run('prepare-remount-seal',['--prepare-only',fixture/'game.unx','--headless'])
            assert 'cache=hit' in text
        finally:
            del env['UNDERTALE_VERIFY_CACHE']
        remount_proof=[]
        for path in (fixture/'game.unx',generation/'ATLAS.RGBA4444'):
            stamp=path.stat();temp=path.with_name(path.name+'.new')
            content=path.read_bytes();temp.write_bytes(content)
            os.utime(temp,ns=(stamp.st_atime_ns,stamp.st_mtime_ns));os.replace(temp,path)
            after=path.stat()
            assert after.st_size==stamp.st_size and after.st_mtime_ns==stamp.st_mtime_ns
            assert path.read_bytes()==content and after.st_ino!=stamp.st_ino
            remount_proof.append(dict(file=path.name,bytes=after.st_size,mtime_ns=after.st_mtime_ns,
                sha256=hashlib.sha256(content).hexdigest(),inode_changed=True))
        report['remount_identity']=remount_proof
        text=run('prepare-remount-identity',['--prepare-only',fixture/'game.unx','--headless'])
        assert 'validation=sealed-metadata' in text
        original_cache=env['FROGGY_CACHE_DIR']
        env['FROGGY_CACHE_DIR']=str(a.out/'failure-cache');env['UNDERTALE_PREP_FAIL_BEFORE_COMMIT']='1'
        run('prepare-failure',['--prepare-only',fixture/'game.unx','--headless'],1)
        assert not list((a.out/'failure-cache').glob('*/complete.txt'))
        assert not list((a.out/'failure-cache').glob('.stage-*'))
        del env['UNDERTALE_PREP_FAIL_BEFORE_COMMIT']
        text=run('prepare-retry',['--prepare-only',fixture/'game.unx','--headless'])
        assert 'cache=created' in text
        env['FROGGY_CACHE_DIR']=str(a.out/'cancel-cache');env['UNDERTALE_PREP_CANCEL_AT']='15'
        run('prepare-cancel',['--prepare-only',fixture/'game.unx','--headless'],130)
        assert not list((a.out/'cancel-cache').glob('*/complete.txt'))
        del env['UNDERTALE_PREP_CANCEL_AT'];env['FROGGY_CACHE_DIR']=original_cache
        text=run('full-loop',[fixture/'game.unx','--headless','--exit-at-frame','6','--trace-frames'])
        assert 'FROGGY_EXIT status=0' in text
        assert 'frames=6 ' in text or 'frames=7 ' in text
        run('missing-data',['--check-data',a.out/'missing.unx'],1)
        bad=a.out/'bad.unx';bad.write_bytes(b'FORM\xff\xff\xff\xffGEN8')
        run('malformed-wad',['--check-data',bad],1)
        atlas=fixture/'ATLAS.BIN';original=atlas.read_bytes()
        atlas.write_bytes(original[:8]+b'\xff'*4+original[12:])
        run('bad-atlas',['--check-data',fixture/'game.unx'],1)
        atlas.write_bytes(original)
        run('missing-grid-launch',[],66)
        report['passed']=True
    finally:
        transport_dir.cleanup()
        (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    main()
