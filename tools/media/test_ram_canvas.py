"""Test the production RAM renderer on host and MIPS without any video backend."""
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
        font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
        if target=='mips':
            cc=str(B/'host/bin/mipsel-buildroot-linux-musl-g++');pkg=str(B/'host/bin/pkg-config')
            env.update(PKG_CONFIG_SYSROOT_DIR=str(B/'staging'),PKG_CONFIG_PATH='',
                PKG_CONFIG_LIBDIR=str(B/'staging/usr/lib/pkgconfig'))
            font=str(B/'target/usr/share/fonts/dejavu/DejaVuSans.ttf')
            run=['qemu-mipsel','-cpu','74Kf','-L',str(B/'staging')]
        else:
            flags=['-fsanitize=address,undefined','-no-pie']
            env.update(ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1')
        libs=shlex.split(subprocess.check_output([pkg,'--cflags','--libs','sdl2','SDL2_image','SDL2_ttf'],env=env,text=True))
        binary=a.out/target
        source=P/'package/lilsmoke-ui/src/src'
        result=subprocess.run([cc,'-std=c++17','-O1','-g',*flags,'-I'+str(source),
            str(P/'tools/media/test_ram_canvas.cpp'),*[str(source/f) for f in ('Renderer.cpp','Font.cpp','Log.cpp')],
            *libs,'-ldl','-lwebp','-o',str(binary)],env=env,text=True,capture_output=True,timeout=180)
        (a.out/(target+'-build.log')).write_text(result.stdout+result.stderr);assert result.returncode==0,result.stderr
        result=subprocess.run([*run,str(binary),font,'/root/lilsmoke-media-fixtures/Photo.webp'],
            env=env,text=True,capture_output=True,timeout=30)
        (a.out/(target+'.log')).write_text(result.stdout+result.stderr)
        assert result.returncode==0,(target,result.stdout,result.stderr)
    (a.out/'report.json').write_text(json.dumps(dict(passed=True,targets=['host-asan','mips'],
        actual_renderer=True,video_driver_required=False,physical_overlay_tested=False),indent=2)+'\n')
    print('PASS real RAM renderer with no video driver, host ASan + MIPS')
if __name__=='__main__':main()
