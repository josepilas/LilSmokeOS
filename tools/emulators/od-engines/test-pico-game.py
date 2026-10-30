"""Private MD ZIP, genuine native PicoDrive SDL software and menu test."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
P=Path(__file__).resolve().parents[3]
R=Path('/root/lilsmoke-emulator-expansion-20261008')
B=Path('/root/lilsmoke-os/build/buildroot/output')
S=R/'engine-sysroot'
out=P/'test-results/opendingux-pico-gba-2026-10-09/pico-game'
out.mkdir(parents=True,exist_ok=True)
rom=P.parents[2]/'work/m8-added-roms/md.zip'
original=hashlib.sha256(rom.read_bytes()).hexdigest()
home=R/'pico-qa/home'
home.mkdir(parents=True,exist_ok=True)
libs=':'.join(map(str,[R/'od-console-qa-runtime/runtime/lib',R/'built-native/lib',S/'usr/lib',S/'lib']))
env=dict(os.environ,HOME=str(home),LILSMOKE_OD_NATIVE='1',LILSMOKE_ARCHIVE_CACHE=str(home/'archive-cache'),
    LILSMOKE_OD_SHARE_DIR=str(R/'od-console-stage/storage/emulators/share/picodrive'),
    SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',
    PICOFE_SW_ONLY='1',PICOFE_GL='0',OD_QA_OUTPUT=str(out),
    OD_QA_BEFORE_AT='18',OD_QA_AFTER_AT='35',OD_QA_QUIT_AT='40',
    OD_QA_KEYS='20:13:0.2,25:275:0.4,26:122:0.3,33:27:0.2')
resolved=subprocess.check_output(['qemu-mipsel','-cpu','74Kf','-L',str(S),
    '-E','LD_LIBRARY_PATH='+libs,str(B/'target/usr/lib/lilsmoke/lilsmoke-rom'),'md',str(rom)],env=env,text=True).strip()
shim=home/'observer.so'
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-shared','-fPIC','-O1',
    '-I'+str(S/'usr/include/SDL'),str(P/'tools/emulators/od-consoles/qa-sdl12.c'),
    '-lSDL','-ldl','-o',str(shim)],check=True)
binary=R/'od-console-stage/storage/emulators/opendingux/picodrive'
with (out/'run.log').open('w') as log:
    result=subprocess.run(['qemu-mipsel','-cpu','74Kf','-L',str(S),'-E','LD_LIBRARY_PATH='+libs,
        '-E','LD_PRELOAD='+str(shim),str(binary),resolved],cwd=home,env=env,stdout=log,stderr=log,timeout=55)
assert result.returncode==0,(result.returncode,(out/'run.log').read_text())
hashes={}
for name in ('before','after'):
    frame=Image.open(out/(name+'.bmp')).convert('RGB');frame.save(out/(name+'.png'))
    assert len(frame.getcolors(frame.width*frame.height))>4,'blank framebuffer'
    hashes[name]=hashlib.sha256(frame.tobytes()).hexdigest()
assert hashes['before']!=hashes['after']
assert hashlib.sha256(rom.read_bytes()).hexdigest()==original
(out/'result.json').write_text(json.dumps(dict(passed=True,frames=hashes,
    sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),archive_unchanged=True,
    scope='MIPS native PicoDrive SDL software, MD ZIP resolved, Start/movement/button/menu/quit',
    physical_hardware_tested=False,hardware_audio_tested=False),indent=2)+'\n')
print('PASS native PicoDrive MD ZIP, software game video, input/menu and quit')
