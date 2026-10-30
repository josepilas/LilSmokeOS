import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time
import socket
import signal
from PIL import Image
P=Path(__file__).resolve().parents[3]
R=Path('/root/lilsmoke-emulator-expansion-20261008')
B=Path('/root/lilsmoke-os/build/buildroot/output')
S=R/'engine-sysroot'
out=P/('test-results/opendingux-native-video-2026-10-09/nx-game' if '--native-video' in sys.argv else 'test-results/opendingux-completion-2026-10-09/nx-game')
out.mkdir(parents=True,exist_ok=True)
qa=R/'nx-qa'
shim=qa/'game-observer.so'
subprocess.run([str(B/'host/bin/mipsel-buildroot-linux-musl-gcc'),'-shared','-fPIC','-O1','-g',
               '-I'+str(S/'usr/include/SDL2'),str(P/'tools/emulators/od-engines/game-observer.c'),
               '-lSDL2','-ldl','-o',str(shim)],check=True)
importer=qa/'importer-shim'
command=['qemu-mipsel','-cpu','74Kf','-L',str(B/'staging'),str(R/'built-native/bin/lilsmoke-nx-data')]
importer.write_text('#!/bin/sh\nexec '+shlex.join(command)+' "$@"\n');importer.chmod(0o700)
data=Path(json.loads((P/'test-results/opendingux-completion-2026-10-09/nx-import.json').read_text())['data'])
home=qa/'home';home.mkdir(exist_ok=True)
env=dict(os.environ,HOME=str(home),XDG_CONFIG_HOME=str(home),XDG_DATA_HOME=str(home),
         LILSMOKE_OD_NATIVE='1',
         SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',
         LILSMOKE_NX_IMPORTER=str(importer),LILSMOKE_NX_SUPPORT=str(R/'built-native/data/share/nxengine/data'),
         LILSMOKE_NX_CACHE=str(qa/'cache'),LILSMOKE_NX_EXTRACTOR=str(qa/'nxextract-shim'),OD_GAME_QA_OUT=str(out))
libs=':'.join(map(str,[R/'engine-build/sdl-qa',S/'usr/lib',S/'lib']))
if '--debug' in sys.argv:
    env['OD_GAME_QA_DEBUG']='1'
    with socket.socket() as s: s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    with (out/'debug-run.log').open('w') as log, (out/'backtrace.txt').open('w') as trace:
        vm=subprocess.Popen(['qemu-mipsel','-g',str(port),'-cpu','74Kf','-L',str(S),
            '-E','LD_LIBRARY_PATH='+libs,'-E','LD_PRELOAD='+str(shim),
            str(R/'engine-build/nxengine/nxengine-evo'),'--data',str(data)],env=env,cwd=qa,stdout=log,stderr=log)
        debugger=None
        try:
            time.sleep(1)
            debugger=subprocess.Popen(['gdb-multiarch','-q','-batch',str(R/'engine-build/nxengine/nxengine-evo'),
                '-ex','set sysroot /','-ex','set solib-search-path '+':'.join(map(str,[qa,R/'engine-build/sdl-qa',S/'usr/lib',S/'lib'])),
                '-ex','target remote 127.0.0.1:'+str(port),
                '-ex','break run_tick()','-ex','continue','-ex','break SDL_Delay','-ex','continue',
                '-ex','thread apply all bt','-ex','info registers a0 a1 ra','-ex','p/x od_debug_pc',
                '-ex','info symbol od_debug_pc','-ex','info symbol od_debug_ra',
                '-ex','x/8i od_debug_pc','-ex','x/64wx od_debug_sp',
                '-ex','info symbol 0x5566a444','-ex','info symbol 0x2b440c54',
                '-ex','info symbol 0x2b494760','-ex','info symbol 0x2b491c7c',
                '-ex','info symbol 0x2b496754','-ex','info symbol 0x2b3453b0',
                '-ex','detach'],stdout=trace,stderr=trace)
            debugger.wait(timeout=40)
        finally:
            if debugger and debugger.poll() is None: debugger.kill();debugger.wait()
            vm.terminate()
            try:vm.wait(timeout=3)
            except subprocess.TimeoutExpired:vm.kill();vm.wait()
    print((out/'backtrace.txt').read_text());raise SystemExit(0)
with (out/'run.log').open('w') as log:
    preload=str(shim)
    if '--native-video' in sys.argv:
        env.update(LILSMOKE_OD_QA='1',LILSMOKE_OD_ENGINE='nxengine',LILSMOKE_HCFB='1')
        q=R/'engine-build/native-video-qa'
        preload+=':'+str(q/'libod-video-qa.so')+':'+str(q/'presenter-mock.so')
    result=subprocess.run(['qemu-mipsel','-cpu','74Kf','-L',str(S),'-E','LD_LIBRARY_PATH='+libs,
        '-E','LD_PRELOAD='+preload,str(R/'built-native/bin/nxengine'),'--data',str(data)],
        env=env,cwd=qa,stdout=log,stderr=subprocess.STDOUT,timeout=70)
assert result.returncode==0,(result.returncode,(out/'run.log').read_text())
if '--native-video' in sys.argv:
    assert 'QA_NATIVE_PRESENT source=320x240' in (out/'run.log').read_text()
hashes={}
for name in ('before','after'):
    frame=Image.open(out/(name+'.bmp')).convert('RGB');frame.save(out/(name+'.png'))
    assert len(frame.getcolors(frame.width*frame.height))>4,'blank/non-game framebuffer'
    hashes[name]=hashlib.sha256(frame.tobytes()).hexdigest()
assert hashes['before']!=hashes['after'],'no game progression'
(out/'result.json').write_text(json.dumps(dict(passed=True,game_frames=hashes,
    scope='native MIPS software SDL game with keyboard navigation/movement/jump and quit',
    native_video_bridge_tested='--native-video' in sys.argv,
    presenter_mocked='--native-video' in sys.argv,
    physical_hardware_tested=False,hardware_audio_tested=False,game_data_bundled=False),indent=2)+'\n')
print('PASS native NXEngine data import, software rendering, input/progression and quit')
