#!/usr/bin/env python3
"""Read-only ROM tests; private data/caches are removed, reports contain metrics."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_private_rom import mio0

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--build',type=Path,required=True)
    args=p.parse_args()
    before=hashlib.sha256(args.rom.read_bytes()).hexdigest()
    raw=args.rom.read_bytes()
    assert hashlib.sha1(raw).hexdigest()=='9bef1128717f958171a4afac3ed78ee2bb4e86ce'
    data=mio0(raw,4178608)[0xe958:0x1104c]
    def word(at): return int.from_bytes(data[at:at+2],'big')
    assert word(0)==0x40 and word(2)==570
    at=4+570*6; count=0; groups={}
    while word(at)!=0x41:
        kind,n=word(at),word(at+2)
        assert kind<0x40 or 0x65<=kind<=0xff
        assert n>0
        count+=n;groups[hex(kind)]=n
        at+=4+n*(8 if kind in (4,14,36,37,39,44,45) else 6)
        assert at<len(data)
    results={}
    try:
        with tempfile.TemporaryDirectory(prefix='sm64-private-collision-',dir='/root') as temporary:
            work=Path(temporary);original=work/'original';original.mkdir()
            shutil.copyfile(args.rom,original/'any name.private')
            for target in ('host','mips','asan'):
                command=[str(args.build/('test-'+target))]
                if target=='mips':command=['qemu-mipsel','-cpu','74Kf','-L',str(args.sdk/'staging'),*command]
                run=subprocess.run([*command,'--original',str(original),str(work/(target+'.pdat'))],
                                   capture_output=True,text=True,timeout=90)
                assert run.returncode==0,(target,run.stdout,run.stderr)
                assert 'runtime error:' not in run.stderr and 'AddressSanitizer' not in run.stderr,run.stderr
                found=re.search(r'BobCollisionSurfaces=(\d+) SpatialReferences=(\d+) Spawn=([^\n]+)',run.stdout)
                assert found and int(found[1])==count,run.stdout
                results[target]=dict(passed=True,collision_surfaces=int(found[1]),spatial_references=int(found[2]),
                                     spawn=found[3],checks=run.stdout.strip().splitlines())
            report=dict(passed=True,rom_sha256=before,rom_unchanged=True,collision_vertices=570,
                        collision_groups=groups,independently_decoded_surfaces=count,targets=results)
            (args.build/'private-collision.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report,indent=2))
    finally:
        assert hashlib.sha256(args.rom.read_bytes()).hexdigest()==before

if __name__=='__main__':main()
