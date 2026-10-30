"""Real MIPS frontend/core callback test; synthetic input, not physical FPS QA."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
Q=Path(tempfile.mkdtemp(prefix='ra-analog-frontend-',dir='/root'))
cc=B/'host/bin/mipsel-buildroot-linux-musl-gcc'
subprocess.run([str(cc),'-O2','-shared','-fPIC','-I'+str(B/'build/retroarch-mips-v1.19.1/libretro-common/include'),
    str(P/'tools/gamepads/retroarch_analog_core.c'),'-o',str(Q/'qa_libretro.so')],check=True)
subprocess.run([str(cc),'-O2','-I'+str(P/'package/lilsmoke-ui/src/src'),
    str(P/'tools/gamepads/retroarch_analog_fixture.c'),'-o',str(Q/'producer')],check=True)
(Q/'bin').mkdir()
(Q/'bin/resolver').write_text('#!/bin/sh\nprintf "%s\\n" "$2"\n')
(Q/'bin/resolver').chmod(0o700)
# Only this input diagnostic disables video. Production remains DirectFB.
(Q/'bin/retroarch').write_text('''#!/usr/bin/python3
import os,sys,subprocess
from pathlib import Path
args=sys.argv[1:];file=Path(args[args.index('--config')+1])
rows=file.read_text().splitlines()
rows=[line for line in rows if line.split('=',1)[0].strip() not in
      ('audio_driver','audio_enable','video_driver','video_vsync','video_threaded','video_frame_delay','input_driver')]
file.write_text('video_driver = "null"\\naudio_enable = false\\naudio_driver = "null"\\nvideo_vsync = false\\nvideo_threaded = false\\ninput_driver = "sdl2"\\n'+'\\n'.join(rows)+'\\n')
os.environ['SDL_VIDEODRIVER']='dummy';os.environ['SDL_AUDIODRIVER']='dummy'
os.execvp('qemu-mipsel',['qemu-mipsel','-cpu','74Kf','-L',os.environ['QA_TARGET'],os.environ['QA_RA'],*args,'--max-frames','125'])
''')
(Q/'bin/retroarch').chmod(0o700)
(Q/'Game.gba').write_bytes(b'synthetic fixture, no game assets')
(Q/'system.conf').write_text('DISPLAY_WIDTH=320\nDISPLAY_HEIGHT=240\n')
(Q/'base.cfg').write_text('video_driver = "sdl2"\ninput_player1_joypad_index = 0\nquit_on_close_content = 1\n')
env=os.environ|{'PATH':str(Q/'bin')+':/usr/bin:/bin','QA_TARGET':str(B/'target'),
    'QA_RA':str(B/'target/usr/bin/retroarch'),'LILSMOKE_HCFB':'1',
    'LILSMOKE_CONFIG':str(Q/'system.conf'),'LILSMOKE_CORES_DIR':str(Q),
    'LILSMOKE_EXTRA_CORES_DIR':str(Q/'extra'),'LILSMOKE_GAMEPAD_STATE':str(Q/'state.bin'),
    'LILSMOKE_INPUT_DIR':str(Q/'no-input'),'LILSMOKE_ROM_RESOLVER':str(Q/'bin/resolver'),
    'LILSMOKE_RETROARCH_BASE_CONFIG':str(Q/'base.cfg'),'LILSMOKE_RETROARCH_USER_CONFIG':str(Q/'user.cfg'),
    'LILSMOKE_RETROARCH_RUN_DIR':str(Q/'run'),'LILSMOKE_LOG_DIR':str(Q/'logs'),
    'LILSMOKE_RETROARCH_AUTOCONFIG':str(Q/'autoconfig'),
    'LILSMOKE_RETROARCH_PROFILES':str(Q/'profiles'),'LILSMOKE_RETROARCH_CORE_OPTIONS':str(Q/'options.cfg')}
for key,value in {'SAVE_ROOT':'saves','STATE_ROOT':'states','SCREENSHOT_DIR':'screens',
                  'PLAYLIST_DIR':'lists','THUMBNAIL_DIR':'thumbnails','ARCHIVE_CACHE':'cache'}.items():
    env['LILSMOKE_'+key]=str(Q/value)
producer=subprocess.Popen(['qemu-mipsel','-cpu','74Kf','-L',str(B/'target'),str(Q/'producer')],env=env)
try:
    for mode in ('analog','digital'):
        if mode=='digital':env['QA_DIGITAL_ONLY']='1'
        process=subprocess.Popen(['busybox','sh',str(P/'package/lilsmoke-scripts/src/lilsmoke-retroarch'),
            'run','gba',str(Q/'Game.gba'),'qa_libretro.so'],env=env)
        try:
            deadline=time.monotonic()+30
            log_path=Q/'logs/retroarch.log'
            while True:
                log=log_path.read_text(errors='replace') if log_path.exists() else ''
                if 'QA_ANALOG_CORE mode='+mode in log:break
                if process.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError('Input callback test failed: '+log[-6000:])
                time.sleep(.05)
        finally:
            # Content shutdown normally returns this appliance to its menu.
            # This input-only probe terminates the isolated diagnostic process.
            if process.poll() is None:process.terminate()
            process.wait(timeout=10)
        log=(Q/'logs/retroarch.log').read_text(errors='replace')
        (Q/(mode+'.log')).write_text(log)
        assert 'QA_ANALOG_CORE mode='+mode+' passed=1' in log,log[-6000:]
        assert 'Found joypad driver: "lilsmoke"' in log
finally:
    producer.terminate();producer.wait(timeout=10)
(Q/'report.json').write_text(json.dumps(dict(passed=True,actual_mips_frontend=True,
    both_sticks=True,ports=4,digital_core_left_stick=True,synthetic_input=True,
    null_video_input_test=True,diagnostic_process_stopped_after_result=True,
    physical_console_tested=False,hardware_fps_measured=False),indent=2)+'\n')
print('PASS actual MIPS RetroArch frontend: four ports, both sticks, digital-game fallback',Q)
