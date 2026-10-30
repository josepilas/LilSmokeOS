#!/usr/bin/env python3
"""Private native gameplay validation: no ROM/cache files survive this test."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from PIL import Image

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--sdk',type=Path)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--pictures',action='store_true')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    before=hashlib.sha256(args.rom.read_bytes()).hexdigest()
    command=[str(args.binary)]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',UBSAN_OPTIONS='halt_on_error=1',
             ASAN_OPTIONS='detect_leaks=1')
    if args.sdk:
        command=['qemu-mipsel','-cpu','74Kf','-L',str(args.sdk/'staging'),*command]
        env['LD_LIBRARY_PATH']=str(args.sdk/'staging/usr/lib')+':'+str(args.sdk/'staging/lib')
    try:
        with tempfile.TemporaryDirectory(prefix='sm64-native-private-',dir='/root') as temporary:
            work=Path(temporary);original=work/'original';original.mkdir()
            shutil.copyfile(args.rom,original/'arbitrary dumped ROM')
            cache=work/'.sm64';cache.mkdir();pack=cache/'bob-us-v1.pdat';report={}
            icon=Path(__file__).resolve().parents[3]/'package/portsmoke-sm64/src/icon.webp'
            for name,frames,scripted in [('spawn',1,False),('walking',26,True),('jump',41,True),('pause',68,True),('complete',150,True),('controller',150,True),('goomba',1,False)]:
                image=work/(name+'.bmp')
                run=subprocess.run([*command,'--original',str(original),'--pack',str(pack),'--headless',
                                    '--frames',str(frames),'--snapshot',str(image),'--icon',str(icon),
                                    *(['--gameplay-pad-check' if name=='controller' else '--gameplay-check'] if scripted else []),
                                    *(['--object-preview'] if name=='goomba' else [])],env=env,
                                   capture_output=True,text=True,timeout=240)
                (args.out/(name+'.log')).write_text(run.stdout+run.stderr)
                assert run.returncode==0,(name,run.stdout,run.stderr)
                assert 'runtime error:' not in run.stderr and 'AddressSanitizer' not in run.stderr,run.stderr
                assert 'MarioParts=' in run.stdout and 'SM64_NATIVE_MOVEMENT_PROTOTYPE_NOT_FULL_GAME' in run.stdout
                assert 'NativeLoadingIcon=WebP Size=128x128' in run.stdout
                assert not (original/'arbitrary dumped ROM').exists(),'private app original not removed after verification'
                if name=='spawn':assert 'OriginalRemoved=1' in run.stdout,run.stdout
                else:assert 'UsedCache=1' in run.stdout,run.stdout
                if name in ('complete','controller'):assert 'NativeGameplayCheck=PASS' in run.stdout,run.stdout
                im=Image.open(image).convert('RGB')
                assert im.size==(320,240)
                colors=im.getcolors(320*240);assert colors and len(colors)>50,(name,'blank/insufficient rendered color')
                report[name]=dict(frames=frames,pixel_sha256=hashlib.sha256(im.tobytes()).hexdigest(),colors=len(colors),
                                  log=run.stdout.strip().splitlines())
                if args.pictures:im.save(args.out/(name+'.png'))
            assert len({report[n]['pixel_sha256'] for n in ('spawn','walking','jump','pause','complete')})==5,'rendered gameplay states identical'
            result=dict(passed=True,target='MIPS-user' if args.sdk else 'host',cpu_only=True,
                        full_game=False,hardware_fps_claimed=False,pc_rom_unchanged=True,private_app_rom_deleted_after_verification=True,
                        native_webp=True,rom_sha256=before,states=report)
            (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    finally:
        assert hashlib.sha256(args.rom.read_bytes()).hexdigest()==before

if __name__=='__main__':main()
