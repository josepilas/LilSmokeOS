"""Actual decoder PCM invariants and production Player native-failure recovery."""
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
    os.nice(10);os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
    fixtures=Path('/root/lilsmoke-media-fixtures');gates=[]
    def run(name,cmd,env):
        r=subprocess.run(list(map(str,cmd)),env=env,text=True,capture_output=True,timeout=240)
        (a.out/(name+'.log')).write_text(r.stdout+r.stderr)
        assert r.returncode==0,(name,r.stdout[-1500:],r.stderr[-2500:])
        gates.append(name)
    for target in ('host','mips'):
        env=os.environ.copy();cc='g++';pkg='pkg-config';flags=[];execute=[]
        if target=='mips':
            env.update(PKG_CONFIG_SYSROOT_DIR=str(B/'staging'),PKG_CONFIG_PATH='',PKG_CONFIG_LIBDIR=str(B/'staging/usr/lib/pkgconfig'))
            cc=str(B/'host/bin/mipsel-buildroot-linux-musl-g++');pkg=str(B/'host/bin/pkg-config')
            execute=['qemu-mipsel','-cpu','74Kf','-L',B/'staging']
        else:
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
            env.update(ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
        config=shlex.split(subprocess.check_output([pkg,'--cflags','--libs','libavformat','libavcodec','libavutil','libswscale','libswresample'],env=env,text=True))
        binary=a.out/('late-'+target)
        run('build-late-'+target,[cc,'-std=c++17','-O1','-g',*flags,'-I'+str(P/'package/lilsmoke-media/src'),P/'tools/media/test_late_video.cpp',P/'package/lilsmoke-media/src/Decoder.cpp',*config,'-o',binary],env)
        run('late-'+target,[*execute,binary,*[fixtures/n for n in ('Clip.mp4','Clip.webm','Clip.mkv')]],env)
        folder=a.out/('fallback-'+target)
        env.update(LILSMOKE_MEDIA_MAIN=str(P/'tools/media/test_player_fallback.cpp'),LILSMOKE_MEDIA_SANITIZE='1')
        run('build-fallback-'+target,['bash',P/('tools/media/build_mips.sh' if target=='mips' else 'tools/media/build_host.sh'),folder],env)
        run('fallback-'+target,[*execute,folder/'lilsmoke-media',fixtures/'Clip.mp4'],env)
    (a.out/'report.json').write_text(json.dumps(dict(passed=True,gates=gates,pcm_unchanged=True,unknown_duration_tested_separately=True,native_decoder_simulated=True,physical_decoder_tested=False),indent=2)+'\n')
    print('PASS late-frame conversion/PCM invariants and real Player fallback, host+MIPS')

if __name__=='__main__':main()
