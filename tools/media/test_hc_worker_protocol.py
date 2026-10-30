"""Exercise the real firmware adapter with a small asynchronous library double."""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
import time

P=Path(__file__).resolve().parents[2]
LIBRARY=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
struct player { int polls,playing; int64_t position; };
int hcplayer_init(int n){(void)n;return 0;}
void *hcplayer_create(void *args){
    /* This host double tests lifecycle, not the worker's MIPS32 argument ABI. */
    (void)args;const char *file=getenv("HC_TEST_FILE");
    if(!strcmp(file,"/device-error")){int fd=open("/dev/viddec",O_RDWR);if(fd>=0)close(fd);}
    return calloc(1,sizeof(struct player));
}
void hcplayer_stop2(void *p,int a,int b){(void)a;(void)b;free(p);}
void hcplayer_play(void *p){((struct player *)p)->playing=1;}
void hcplayer_pause(void *p){((struct player *)p)->playing=0;}
void hcplayer_resume(void *p){hcplayer_play(p);}
int hcplayer_seek(void *p,int64_t at){((struct player *)p)->position=at;return 0;}
int64_t hcplayer_get_duration(void *p){return ++((struct player *)p)->polls<3?-22:30000;}
int64_t hcplayer_get_position(void *p){struct player *v=p;if(v->playing)v->position+=200;return v->position;}
int hcplayer_set_display_rect(void *p,void *r){(void)p;(void)r;return 0;}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    if Path('/dev/viddec').exists():raise RuntimeError('This double requires no physical decoder')
    library=a.out/'fake.c';library.write_text(LIBRARY)
    subprocess.run(['cc','-shared','-fPIC','-O1',str(library),'-o',str(a.out/'libffplayer.so')],check=True)
    binary=a.out/'worker'
    subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wl,--export-dynamic',
        str(P/'package/lilsmoke-media/src/hcplayer_worker.c'),'-ldl','-o',str(binary)],check=True)
    env=os.environ|{'LD_LIBRARY_PATH':str(a.out)}
    def start(file):
        p=subprocess.Popen([str(binary),file],env=env|{'HC_TEST_FILE':file},stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
        return p
    def line(p):
        if not select.select([p.stdout],[],[],3)[0]:raise TimeoutError('worker status')
        return p.stdout.readline().decode().strip()
    def command(p,text):p.stdin.write((text+'\n').encode())
    transcript=[]
    p=start('/unknown')
    try:
        first=line(p);transcript.append(first);assert first=='HC_READY -1',first
        command(p,'PLAY');duration=False;position=False;deadline=time.monotonic()+3
        while not(duration and position) and time.monotonic()<deadline:
            text=line(p);transcript.append(text)
            duration|=text=='HC_DURATION 30000'
            position|=text.startswith('HC_POSITION ') and int(text.split()[1])>0
        assert duration and position
        command(p,'QUIT');out,err=p.communicate(timeout=3)
        transcript.extend(out.decode().splitlines());assert p.returncode==0 and b'HC_STOPPED' in out,err
    finally:
        if p.poll() is None:p.kill();p.wait()
    sdk=Path('/root/lilsmoke-os/build/buildroot/output')
    for target in ('host','mips'):
        compiler='cc' if target=='host' else str(sdk/'host/bin/mipsel-buildroot-linux-musl-gcc')
        trace=a.out/('trace-'+target)
        subprocess.run([compiler,'-std=gnu11','-O1','-Wall','-Wextra','-Werror',
            str(P/'tools/media/test_hc_device_trace.c'),'-ldl','-o',str(trace)],check=True)
        execute=[] if target=='host' else ['qemu-mipsel','-cpu','74Kf','-L',str(sdk/'staging')]
        result=subprocess.run([*execute,str(trace)],text=True,capture_output=True,timeout=20)
        (a.out/('trace-'+target+'.log')).write_text(result.stdout+result.stderr)
        assert result.returncode==0 and 'HC_DEVICE_TRACE_PASS' in result.stdout,(target,result.stdout,result.stderr)
    p=start('/device-error')
    try:
        out,err=p.communicate(timeout=3)
        transcript.extend(out.decode().splitlines())
        assert p.returncode==5 and b'HC_ERROR viddec-open errno=2' in out and b'HC_DEVICE_OPEN' in err,(p.returncode,out,err)
    finally:
        if p.poll() is None:p.kill();p.wait()
    (a.out/'worker.log').write_text('\n'.join(transcript)+'\n')
    (a.out/'report.json').write_text(json.dumps(dict(passed=True,real_worker_source=True,firmware_library_simulated=True,
        asynchronous_duration=True,device_errno_propagated=True,device_trace_host_mips=True,
        firmware_negative_duration_normalized=True,physical_decoder_tested=False),indent=2)+'\n')
    print('PASS actual worker asynchronous duration and decoder-open error protocol (library double)')

if __name__=='__main__':main()
