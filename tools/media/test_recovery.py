#!/usr/bin/env python3
"""Decoder/PCM regression gates; optional owner media stays private."""
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
    parser.add_argument('--media',type=Path,nargs='+',required=True)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    os.nice(10)
    os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
    results=[]
    media=[]
    for index,source in enumerate(args.media):
        # Keep the exact small, badly interleaved MP4. Long owner recordings
        # are trimmed by packet copy, not decoded for minutes under TCG.
        if source.suffix.lower()=='.mp4' and source.stat().st_size<1024*1024:
            media.append(source)
        else:
            clip=args.out/('private-input-'+str(index)+source.suffix)
            subprocess.run(['ffmpeg','-nostdin','-v','error','-i',str(source),'-t','8',
                '-map','0','-c','copy',str(clip)],check=True,timeout=60)
            media.append(clip)
    def run(name,command,env=None):
        result=subprocess.run([str(a) for a in command],env=env,text=True,
            capture_output=True,timeout=240)
        (args.out/(name+'.log')).write_text(result.stdout+result.stderr)
        assert result.returncode==0,(name,result.stdout[-2000:],result.stderr[-3000:])
        results.append(name)
    packages=['sdl2','libavformat','libavcodec','libavutil','libswscale','libswresample']
    for target in ('host','mips'):
        env=os.environ.copy()
        pkg='pkg-config'
        compiler='g++'
        execute=[]
        flags=[]
        if target=='mips':
            env.update(PKG_CONFIG_SYSROOT_DIR=str(B/'staging'),PKG_CONFIG_PATH='',
                PKG_CONFIG_LIBDIR=str(B/'staging/usr/lib/pkgconfig'))
            pkg=str(B/'host/bin/pkg-config')
            compiler=str(B/'host/bin/mipsel-buildroot-linux-musl-g++')
            execute=['qemu-mipsel','-cpu','74Kf','-L',B/'staging']
        else:
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
            env.update(ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1')
        config=shlex.split(subprocess.check_output([pkg,'--cflags','--libs',*packages],env=env,text=True))
        binary=args.out/('decoder-'+target)
        run('build-decoder-'+target,[compiler,'-std=c++17','-O1','-g',*flags,
            '-I'+str(P/'package/lilsmoke-media/src'),P/'tools/media/recovery_tests.cpp',
            P/'package/lilsmoke-media/src/Decoder.cpp',*config,'-o',binary],env)
        run('decoder-'+target,[*execute,binary,*media],env)
        pacing=args.out/('pacing-'+target)
        run('build-pacing-'+target,[compiler,'-std=c++17','-O1','-g',*flags,
            '-DM8_PCM_INFLIGHT_LIMIT=16U','-I'+str(P/'package/lilsmoke-media/src'),
            '-I'+str(P/'package/opendingux-emulators'),P/'tools/media/test_audio_pacing.cpp',
            P/'package/lilsmoke-media/src/Decoder.cpp',*config,'-o',pacing],env)
        run('pacing-'+target,[*execute,pacing],env)
        for credits in (2,16):
            binary=args.out/f'pcm-{target}-{credits}'
            cc='cc' if target=='host' else str(B/'host/bin/mipsel-buildroot-linux-musl-gcc')
            run(f'build-pcm-{target}-{credits}',[cc,'-std=gnu11','-O1','-g',*flags,
                '-DM8_PCM_INFLIGHT_LIMIT='+str(credits)+'U','-I'+str(P/'board/segam-m8-v6-stock'),
                P/'tools/media/pcm_credit_tests.c','-o',binary],env)
            run(f'pcm-{target}-{credits}',[*execute,binary],env)
        worker=args.out/('worker-'+target)
        cc='cc' if target=='host' else str(B/'host/bin/mipsel-buildroot-linux-musl-gcc')
        run('build-worker-'+target,[cc,'-std=gnu11','-O1','-g',*flags,
            '-DM8_PCM_INFLIGHT_LIMIT=16U','-DM8_PCM_BACKEND_TIMEOUT_MS=500U',
            '-DM8_PCM_BACKEND_MAX_POLLS=256U','-DM8_PCM_ACK_TIMEOUT_MS=1000U',
            '-I'+str(P/'board/segam-m8-v6-stock'),P/'tools/media/test_audio_worker.c',
            '-Wl,--wrap=socketpair','-o',worker],env)
        run('worker-'+target,[*execute,worker],env)
    (args.out/'report.json').write_text(json.dumps(dict(passed=True,gates=results,
        native_audio_backend='mocked, real credit FIFO',owner_media_redistributed=False),indent=2)+'\n')
    print('PASS decoder/clock/PCM host+MIPS:',args.out)

if __name__=='__main__':
    main()
