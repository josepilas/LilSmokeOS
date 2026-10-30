#!/usr/bin/env python3
"""Alternating fixed-work core comparisons with isolated saves and pixel checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--host',type=Path,required=True)
    p.add_argument('--sysroot',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--frames',type=int,default=1200)
    p.add_argument('--repeats',type=int,default=2)
    a=p.parse_args()
    if not 600<=a.frames<=10000 or not 1<=a.repeats<=8: p.error('invalid workload')
    out=a.out.resolve(); out.mkdir(parents=True,exist_ok=False)
    opts=['parallel-n64-gfxplugin=angrylion','parallel-n64-rspplugin=auto',
          'parallel-n64-cpucore=cached_interpreter','parallel-n64-angrylion-multithread=off',
          'parallel-n64-angrylion-vioverlay=Unfiltered','parallel-n64-upscaling=1x']
    manifest=dict(frames=a.frames,repeats=a.repeats,options=opts,
                  input='200-260:3,400-450:0,600-650:0',
                  notice='MIPS user-mode core diagnostic, not full OS or console FPS.',
                  hashes={key:hashlib.sha256(path.read_bytes()).hexdigest() for key,path in
                          [('baseline',a.baseline),('candidate',a.candidate),('rom',a.rom),('host',a.host)]})
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    rows=[]; pixels={}
    for repetition in range(a.repeats):
        order=['baseline','candidate'] if repetition%2==0 else ['candidate','baseline']
        for name in order:
            folder=out/f'{repetition+1}-{name}'; folder.mkdir(); (folder/'lrhost-system').mkdir(); (folder/'frames').mkdir()
            env=dict(os.environ, LRHOST_INPUT=manifest['input'], LRHOST_DUMPDIR=str(folder/'frames'),
                     LRHOST_DUMP_FROM='350',LRHOST_DUMP_TO='370')
            cmd=['qemu-mipsel','-cpu','74Kf','-L',str(a.sysroot.resolve()),str(a.host.resolve()),
                 str(getattr(a,name).resolve()),str(a.rom.resolve()),str(a.frames),*opts]
            with (folder/'run.log').open('wb') as log:
                subprocess.run(cmd,cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=1800,check=True)
            text=(folder/'run.log').read_text(errors='replace')
            m=re.search(r'\| (\d+) frames in ([0-9.]+)s.*mean ([0-9.]+) ms.*presented (\d+) duped (\d+) \| sig (\w+)',text)
            if not m or int(m[1])!=a.frames: raise RuntimeError('incomplete workload: '+str(folder))
            row=dict(case=folder.name,calls=int(m[1]),seconds=float(m[2]),steady_ms=float(m[3]),
                     presented=int(m[4]),duped=int(m[5]),signature=m[6]); rows.append(row)
            hashes={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted((folder/'frames').glob('*'))}
            if not hashes: raise RuntimeError('no verification frames')
            pixels[folder.name]=hashes
            print(json.dumps(row),flush=True)
    (out/'results.json').write_text(json.dumps(rows,indent=2))
    (out/'pixels.json').write_text(json.dumps(pixels,indent=2))
    if any(v!=next(iter(pixels.values())) for v in pixels.values()): raise RuntimeError('pixel mismatch')
    if len({(r['presented'],r['duped'],r['signature']) for r in rows})!=1: raise RuntimeError('core output mismatch')
    print('N64_PAIR_OK')

if __name__=='__main__': main()
