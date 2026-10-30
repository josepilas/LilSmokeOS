#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Real MIPS software-window preparation UI, using the retained QA SDL dummy sink."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from fixture import make

HERE=Path(__file__).resolve().parent
SDK=Path(os.environ.get('SDK','/root/lilsmoke-os/build/buildroot/output'))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--qa-sdl',type=Path,default=Path('/root/lilsmoke-emulator-expansion-20261008/engine-build/sdl-qa'))
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    fixture=make(a.out/'original')
    env=os.environ.copy();env.update(FROGGY_INPUT_DIR='/nonexistent',FROGGY_NO_SHARED_INPUT='1',
        FROGGY_CACHE_DIR=str(a.out/'cache'),FROGGY_CAPTURE_DIR=str(a.out),SDL_VIDEODRIVER='dummy',
        LD_LIBRARY_PATH=str(a.qa_sdl))
    for name in ('LD_PRELOAD','FROGGY_NONINTERACTIVE','LILSMOKE_HCFB','SW_ASSET_DIR'):env.pop(name,None)
    prefix=['taskset','-c',os.environ.get('FROGGY_CPU','1'),'nice','-n','15','qemu-mipsel',
            '-cpu','74Kf','-L',str(SDK/'staging'),str(HERE/'_build/mips/froggytale-lilsmoke')]
    results=[]
    for name in ('first','cached'):
        r=subprocess.run(prefix+['--prepare-only',str(fixture/'game.unx')],env=env,cwd=a.out,
                         stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=90)
        (a.out/(name+'.log')).write_text(r.stdout)
        assert r.returncode==0,r.stdout
        assert ('cache=created' if name=='first' else 'cache=hit') in r.stdout
        results.append(dict(name=name,status=r.returncode))
    data=(a.out/'frame-0001.ppm').read_bytes()
    assert data.startswith(b'P6\n320 240\n255\n') and len(data)==230415
    pixels=data[15:]
    colors=len(set(pixels[i:i+3] for i in range(0,len(pixels),3)))
    assert colors>=3
    (a.out/'report.json').write_text(json.dumps(dict(passed=True,results=results,
        color_count=colors,software_rgb565=True,sink='retained QA SDL dummy, not physical display'),indent=2)+'\n')
    print(a.out)


if __name__=='__main__':main()
