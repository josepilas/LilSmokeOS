"""MIPS SDL1 application-owned menus. No game or performance acceptance."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import shutil
from PIL import Image
P=Path(__file__).resolve().parents[3]
R=Path('/root/lilsmoke-emulator-expansion-20261008')
B=Path('/root/lilsmoke-os/build/buildroot/output')
S=R/'engine-sysroot'
out=P/'test-results/opendingux-completion-2026-10-09/legacy-menus'
out.mkdir(parents=True,exist_ok=True)
shim=R/'engine-build/legacy-menu-qa.so'
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-shared','-fPIC','-O1',
    '-I'+str(S/'usr/include/SDL'),str(P/'tools/emulators/od-consoles/qa-sdl12.c'),
    '-lSDL','-ldl','-o',str(shim)],check=True)
libs=':'.join(map(str,[R/'od-console-qa-runtime/runtime/lib',R/'built-native/lib',S/'usr/lib',S/'lib']))
reports={}
for name in (sys.argv[1:] or ('mame4all','openbor')):
    case=R/'engine-build/legacy-menu-qa'/name
    case.mkdir(parents=True,exist_ok=True)
    for folder in ('roms','home','Paks','Saves','Logs','ScreenShots'):(case/folder).mkdir(exist_ok=True)
    if name=='mame4all':
        shutil.copytree(R/'native-candidates/data/share/mame4all/skins',case/'skins',dirs_exist_ok=True)
    env=dict(os.environ,HOME=str(case/'home'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
        SDL_RENDER_DRIVER='software',LILSMOKE_OD_NATIVE='1',LILSMOKE_OD_ROM_DIR=str(case/'roms'),OD_QA_OUTPUT=str(case),
        LILSMOKE_BOR_PAK_DIR=str(case/'Paks'),LILSMOKE_BOR_SAVE_DIR=str(case/'Saves'),
        OD_QA_BEFORE_AT='5',OD_QA_AFTER_AT='7',OD_QA_QUIT_AT='8',OD_QA_KEYS='6:274:0.1')
    log=out/(name+'.log')
    with log.open('w') as stream:
        result=subprocess.run(['qemu-mipsel','-cpu','74Kf','-L',str(S),'-E','LD_LIBRARY_PATH='+libs,
            '-E','LD_PRELOAD='+str(shim),str(R/'native-candidates/bin'/name)],
            env=env,cwd=case,stdout=stream,stderr=stream,timeout=30)
    hashes={}
    for frame in ('before','after'):
        image=Image.open(case/(frame+'.bmp')).convert('RGB')
        image.save(out/(name+'-'+frame+'.png'))
        assert len(image.getcolors(image.width*image.height))>2,'blank '+name
        hashes[frame]=hashlib.sha256(image.tobytes()).hexdigest()
    assert result.returncode==0,(name,result.returncode,log.read_text())
    reports[name]=dict(menu_and_quit_passed=True,frames=hashes,game_tested=False,
        sha256=hashlib.sha256((R/'native-candidates/bin'/name).read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(dict(passed=True,engines=reports,
    scope='MIPS SDL1 software native menu and quit, no game or physical hardware test'),indent=2)+'\n')
print('PASS native '+', '.join(reports)+' menu and quit')
