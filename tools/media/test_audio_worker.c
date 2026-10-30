#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "../../package/opendingux-emulators/m8_pocketsnes_audio.c"

static unsigned stall_ms;
static uint64_t opened;
static int force_pipe;
int __real_socketpair(int,int,int,int[2]);
int __wrap_socketpair(int domain,int type,int protocol,int sockets[2]){
    if(force_pipe){errno=ENOSYS;return -1;}
    return __real_socketpair(domain,type,protocol,sockets);
}
void m8_audio_init(struct m8_audio *a){memset(a,0,sizeof(*a));}
int m8_audio_open(struct m8_audio *a,int volume){(void)a;(void)volume;opened=milliseconds();return 0;}
int m8_audio_submit(struct m8_audio *a,const void *pcm,size_t frames){
    (void)a;assert(pcm&&frames);return milliseconds()-opened>=stall_ms ? 1:0;
}
int m8_audio_pump(struct m8_audio *a){(void)a;return 1;}
int m8_audio_flush(struct m8_audio *a){(void)a;return 0;}
void m8_audio_close(struct m8_audio *a){(void)a;}
static void nap(void){struct timespec delay={0,2000000};nanosleep(&delay,NULL);}

static void scenario(unsigned stall,int success){
    stall_ms=stall;
    assert(m8_pcm_init(44100,2,16));
    struct m8_pcm_stats stats={0};
    uint64_t deadline=milliseconds()+2000;
    do{m8_pcm_get_stats(&stats);nap();}while(!stats.ready&&!stats.error&&milliseconds()<deadline);
    assert(stats.ready&&!stats.error);
    int16_t pcm[735*2]={0};
    assert(m8_pcm_send(pcm,735)==1);
    const uint64_t start=milliseconds();
    do{m8_pcm_get_stats(&stats);nap();}while(!stats.error&&!stats.delivered_frames&&milliseconds()<deadline);
    if(success)assert(!stats.error&&stats.delivered_frames==735&&stats.inflight==0);
    else assert(stats.error==ETIMEDOUT&&!m8_pcm_active()&&milliseconds()-start<1500);
    assert(m8_pcm_close()==0);
}
int main(void){
    setenv("LILSMOKE_M8_AUDIO","1",1);
    for(force_pipe=0;force_pipe<2;++force_pipe){
        scenario(250,1);
        scenario(2000,0);
    }
    puts("MEDIA_WORKER_WATCHDOG_PASS transient_250ms=survives permanent=bounded-error transports=2 fake_hardware=1 real_worker=1");
}
