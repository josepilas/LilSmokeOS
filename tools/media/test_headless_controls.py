"""Real SDL virtual-controller hotplug into production Input, no desktop keys."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    for target in ('host','mips'):
        env=os.environ.copy();cc='g++';pkg='pkg-config';flags=[];run=[]
        if target=='mips':
            cc=str(B/'host/bin/mipsel-buildroot-linux-musl-g++');pkg=str(B/'host/bin/pkg-config')
            env.update(PKG_CONFIG_SYSROOT_DIR=str(B/'staging'),PKG_CONFIG_PATH='',
                PKG_CONFIG_LIBDIR=str(B/'staging/usr/lib/pkgconfig'))
            run=['qemu-mipsel','-cpu','74Kf','-L',str(B/'staging')]
        else:
            flags=['-fsanitize=address,undefined','-no-pie']
            env.update(ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1')
        libs=shlex.split(subprocess.check_output([pkg,'--cflags','--libs','sdl2','SDL2_image','SDL2_ttf'],env=env,text=True))
        source=P/'package/lilsmoke-ui/src/src';binary=a.out/target
        result=subprocess.run([cc,'-std=c++17','-O1','-g',*flags,'-I'+str(source),
            '-include',str(P/'package/lilsmoke-od-sdl/src/input-wire.h'),
            str(P/'tools/media/test_headless_controls.cpp'),
            *[str(source/f) for f in ('Input.cpp','Keymap.cpp','Renderer.cpp','Font.cpp','Log.cpp')],
            *libs,'-ldl','-lwebp','-o',str(binary)],env=env,text=True,capture_output=True,timeout=240)
        (a.out/(target+'-build.log')).write_text(result.stdout+result.stderr)
        assert result.returncode==0,result.stderr
        result=subprocess.run([*run,str(binary)],env=env,text=True,capture_output=True,timeout=30)
        (a.out/(target+'.log')).write_text(result.stdout+result.stderr)
        assert result.returncode==0,(target,result.stdout,result.stderr)
    (a.out/'report.json').write_text(json.dumps(dict(passed=True,targets=['host-asan','mips'],
        actual_input=True,actual_ram_renderer=True,virtual_controller=True,no_video_driver=True,
        no_desktop_keys_injected=True,physical_controller_tested=False),indent=2)+'\n')
    print('PASS production Input/RAM canvas, virtual gamepad buttons/release/hotplug, host+MIPS')
if __name__=='__main__':main()
