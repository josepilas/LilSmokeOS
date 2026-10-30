"""Private user-ROM test: real MIPS SDL engine, archive resolver and pause menu."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import shlex
import shutil
from PIL import Image
P=Path(__file__).resolve().parents[3]
R=Path('/root/lilsmoke-emulator-expansion-20261008')
B=Path('/root/lilsmoke-os/build/buildroot/output')
S=R/'engine-sysroot'
picker='--picker' in sys.argv
out=P/('test-results/opendingux-native-video-2026-10-09/mgba-game' if '--native-video' in sys.argv else ('test-results/opendingux-completion-2026-10-09/mgba-picker-game' if picker else 'test-results/opendingux-completion-2026-10-09/mgba-game'))
out.mkdir(parents=True,exist_ok=True)
rom=P.parents[2]/'work/m8-added-roms/gba.zip'
original=hashlib.sha256(rom.read_bytes()).hexdigest()
home=R/'mgba-qa/home'
home.mkdir(parents=True,exist_ok=True)
libs=':'.join(map(str,[R/'engine-build/sdl-qa',R/'built-native/lib',S/'usr/lib',S/'lib']))
env=dict(os.environ,HOME=str(home),XDG_CONFIG_HOME=str(home),XDG_DATA_HOME=str(home),
         LILSMOKE_NATIVE_TRACE='1',
         LILSMOKE_ARCHIVE_CACHE=str(home/'archive-cache'),LILSMOKE_OD_NATIVE='1',
         SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',
         OD_GAME_QA_OUT=str(out),OD_GAME_QA_ENGINE='mgba')
resolved=subprocess.check_output(['qemu-mipsel','-cpu','74Kf','-L',str(S),
    '-E','LD_LIBRARY_PATH='+libs,str(B/'target/usr/lib/lilsmoke/lilsmoke-rom'),'gba',str(rom)],env=env,text=True).strip()
shim=home/'game-observer.so'
args=['-C','savegamePath='+str(home),resolved]
if picker:
    folder=home/'roms';folder.mkdir(exist_ok=True)
    shutil.copyfile(rom,folder/'A-packed.zip')
    resolver=home/'resolver-shim'
    command=['qemu-mipsel','-cpu','74Kf','-L',str(S),'-E','LD_LIBRARY_PATH='+libs,str(B/'target/usr/lib/lilsmoke/lilsmoke-rom')]
    resolver.write_text('#!/bin/sh\nexec '+shlex.join(command)+' "$@"\n');resolver.chmod(0o700)
    env.update(LILSMOKE_OD_ROM_DIR=str(folder),LILSMOKE_ROM_RESOLVER=str(resolver))
    args=[]
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-shared','-fPIC','-O1',
    '-I'+str(S/'usr/include/SDL2'),str(P/'tools/emulators/od-engines/game-observer.c'),
    '-lSDL2','-ldl','-o',str(shim)],check=True)
with (out/'run.log').open('w') as log:
    preload=str(shim)
    if '--native-video' in sys.argv:
        env.update(LILSMOKE_OD_QA='1',LILSMOKE_OD_ENGINE='mgba',LILSMOKE_HCFB='1')
        q=R/'engine-build/native-video-qa'
        preload+=':'+str(q/'libod-video-qa.so')+':'+str(q/'presenter-mock.so')
    result=subprocess.run(['qemu-mipsel','-cpu','74Kf','-L',str(S),'-E','LD_LIBRARY_PATH='+libs,
        '-E','LD_PRELOAD='+preload,str(R/'built-native/bin/mgba'),*args],
        cwd=home,env=env,stdout=log,stderr=log,timeout=50)
assert result.returncode==0,(result.returncode,(out/'run.log').read_text())
assert 'OD_NATIVE_MENU engine=mGBA pause=1' in (out/'run.log').read_text()
if '--native-video' in sys.argv:
    assert 'QA_NATIVE_PRESENT source=240x160' in (out/'run.log').read_text()
hashes={}
for name in ('before','after'):
    frame=Image.open(out/(name+'.bmp')).convert('RGB')
    frame.save(out/(name+'.png'))
    assert len(frame.getcolors(frame.width*frame.height))>4,'blank framebuffer'
    hashes[name]=hashlib.sha256(frame.tobytes()).hexdigest()
assert hashes['before']!=hashes['after']
assert hashlib.sha256(rom.read_bytes()).hexdigest()==original
(out/'result.json').write_text(json.dumps(dict(passed=True,frames=hashes,archive_unchanged=True,
    scope='MIPS native mGBA SDL software, user GBA ZIP resolved, buttons, native pause menu, quit',
    native_video_bridge_tested='--native-video' in sys.argv,
    presenter_mocked='--native-video' in sys.argv,
    physical_hardware_tested=False,hardware_audio_tested=False),indent=2)+'\n')
print('PASS native mGBA GBA ZIP, buttons, software video, own pause menu and quit')
