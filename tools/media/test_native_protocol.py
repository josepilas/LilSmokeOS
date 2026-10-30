"""Production native controller with an explicitly simulated decoder process."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess

P=Path(__file__).resolve().parents[2]
FAKE=r"""
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
int main(int argc,char **argv){
    if(argc!=2||getenv("LD_PRELOAD")||getenv("LD_LIBRARY_PATH")||getenv("LD_AUDIT"))return 2;
    struct rlimit memory;
    if(getrlimit(RLIMIT_AS,&memory)||memory.rlim_cur!=64*1024*1024||memory.rlim_max!=64*1024*1024)return 3;
    setbuf(stdout,NULL);
    if(!strcmp(argv[1],"/exit"))return 1;
    if(!strcmp(argv[1],"/malformed")){puts("untrusted invalid status");sleep(2);return 0;}
    if(!strcmp(argv[1],"/overflow")){for(int i=0;i<256;++i)putchar('x');putchar('\n');sleep(2);return 0;}
    if(!strcmp(argv[1],"/unknown")){puts("HC_READY -1");puts("HC_DURATION 30000");}
    else if(!strcmp(argv[1],"/negative")){puts("HC_READY -2");sleep(2);return 0;}
    else if(!strcmp(argv[1],"/device-error")){puts("HC_ERROR viddec-open errno=12");return 4;}
    else puts("HC_READY 30000");
    if(!strcmp(argv[1],"/stuck")){signal(SIGTERM,SIG_IGN);for(;;)sleep(1);}
    long position=0;int playing=0;char line[128];unsigned used=0;
    for(;;){
        struct pollfd p={0,POLLIN,0};int n=poll(&p,1,50);
        if(n>0){
            char c;while(read(0,&c,1)==1){
                if(c!='\n'){if(used<127)line[used++]=c;continue;}
                line[used]=0;used=0;
                if(!strcmp(line,"QUIT"))return 0;
                if(!strcmp(line,"PLAY")||!strcmp(line,"RESUME"))playing=1;
                if(!strcmp(line,"PAUSE"))playing=0;
                if(!strncmp(line,"SEEK ",5))position=strtol(line+5,NULL,10);
            }
        }
        if(playing)position+=200;
        printf("HC_POSITION %ld\n",position);
    }
}
"""

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    runtime=a.out/'runtime';runtime.mkdir()
    (runtime/'fake.c').write_text(FAKE)
    (runtime/'ld.so.1').write_text('#!/bin/sh\nexec "$3" "$4"\n')
    (runtime/'ld.so.1').chmod(0o755)
    subprocess.run(['cc','-O2',str(runtime/'fake.c'),'-o',str(runtime/'hcplayer-worker')],check=True)
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','sdl2'],text=True))
    binary=a.out/'controller-test'
    subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-no-pie',
        '-DLILSMOKE_HCPLAYER_ROOT="'+str(runtime)+'"','-I'+str(P/'package/lilsmoke-media/src'),
        str(P/'tools/media/test_native_protocol.cpp'),str(P/'package/lilsmoke-media/src/NativeVideo.cpp'),
        *flags,'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],env=os.environ|{'ASAN_OPTIONS':'detect_leaks=1',
        'UBSAN_OPTIONS':'halt_on_error=1'},text=True,capture_output=True,timeout=30)
    (a.out/'test.log').write_text(result.stdout+result.stderr)
    assert result.returncode==0,(result.stdout,result.stderr)
    aliases=a.out/'device-test'
    subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-no-pie',
        str(P/'tools/media/test_native_devices.cpp'),*flags,'-o',str(aliases)],check=True)
    alias_test=subprocess.run([str(aliases),str(a.out/'devices')],
        env=os.environ|{'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'},
        text=True,capture_output=True,timeout=30)
    (a.out/'aliases.log').write_text(alias_test.stdout+alias_test.stderr)
    assert alias_test.returncode==0,(alias_test.stdout,alias_test.stderr)
    (a.out/'report.json').write_text(json.dumps(dict(passed=True,production_controller=True,
        decoder_simulated=True,device_aliases_tested=True,isolated_environment=True,
        worker_address_space_limit_mib=64,physical_decoder_tested=False,sanitizers=True),indent=2)+'\n')
    print(result.stdout.strip())

if __name__=='__main__':main()
