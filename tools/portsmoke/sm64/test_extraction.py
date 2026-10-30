#!/usr/bin/env python3
"""Safe deletion tests operate only on temporary ROM copies, never the PC dump."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    project=Path(__file__).resolve().parents[3];src=project/'package/portsmoke-sm64/src'
    units=[str(src/x) for x in ('extraction.cpp','asset_pack.cpp','runtime_assets.cpp','bob_scene.cpp','mario_actor.cpp')]
    binaries={}
    for target in ('host','mips','asan'):
        compiler=str(a.sdk/'host/bin/mipsel-buildroot-linux-musl-g++') if target=='mips' else 'g++'
        flags=['-std=c++17','-O2','-pthread','-Wall','-Wextra','-Werror','-I'+str(src)]
        if target=='mips':flags+=['-static-libstdc++','-static-libgcc']
        if target=='asan':flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
        binary=a.out/('test-'+target)
        subprocess.run([compiler,*flags,str(Path(__file__).with_suffix('.cpp')),*units,'-o',str(binary)],check=True)
        binaries[target]=[str(binary)]
        if target=='mips':binaries[target]=['qemu-mipsel','-cpu','74Kf','-L',str(a.sdk/'staging'),*binaries[target]]
    before=hashlib.sha256(a.rom.read_bytes()).hexdigest();results={}
    try:
        for target,command in binaries.items():
            reports={}
            with tempfile.TemporaryDirectory(prefix='sm64-delete-private-',dir='/root') as temporary:
                root=Path(temporary)
                for case in ('cancel-before','cancel-mid','io-failure','mutate-input','success','swapped2','swapped4'):
                    app=root/case;original=app/'original';cache=app/'.sm64';original.mkdir(parents=True);cache.mkdir()
                    private=original/'test.rom';shutil.copyfile(a.rom,private)
                    if case.startswith('swapped'):
                        data=bytearray(private.read_bytes());stride=int(case[-1]);
                        for i in range(0,len(data),stride):data[i:i+stride]=data[i:i+stride][::-1]
                        private.write_bytes(data)
                    ok=case in ('success','swapped2','swapped4')
                    run=subprocess.run([*command,case,str(original),str(cache)],capture_output=True,text=True,timeout=180)
                    (a.out/(target+'-'+case+'.log')).write_text(run.stdout+run.stderr)
                    assert (run.returncode==0)==ok,(target,case,run.stdout,run.stderr)
                    assert 'runtime error:' not in run.stderr and 'AddressSanitizer' not in run.stderr,run.stderr
                    assert private.exists()!=ok,(case,'unsafe original deletion')
                    if case not in ('mutate-input','swapped2','swapped4') and private.exists():
                        assert hashlib.sha256(private.read_bytes()).hexdigest()==before
                    reports[case]=dict(passed=True,original_deleted=not private.exists(),log=run.stdout.strip().splitlines())
                    if case=='success':
                        expected={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in cache.glob('*.pdat')}
                        assert len(expected)==4
                        again=subprocess.run([*command,'reuse',str(original),str(cache)],capture_output=True,text=True,timeout=180)
                        assert again.returncode==0 and 'UsedCache=1' in again.stdout,again.stdout+again.stderr
                        assert expected=={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in cache.glob('*.pdat')}
                        (cache/'mario-us-v1.pdat').write_bytes(b'corrupted cache')
                        damaged=subprocess.run([*command,'reuse',str(original),str(cache)],capture_output=True,text=True,timeout=180)
                        assert damaged.returncode!=0,'damaged cache silently accepted without original'
                        reports['cache-reuse-and-damage']=dict(passed=True)
            results[target]=reports
        report=dict(passed=True,pc_original_unchanged=True,rom_sha256=before,targets=results)
        (a.out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    finally:
        assert hashlib.sha256(a.rom.read_bytes()).hexdigest()==before

if __name__=='__main__':main()
