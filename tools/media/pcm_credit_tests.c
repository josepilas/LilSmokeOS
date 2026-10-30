#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include "../../package/opendingux-emulators/m8_pocketsnes_audio.c"

/* No physical audio backend is linked. Exercise the real credit FIFO/ACK
 * service with distinct lengths, so an accidentally two-entry shift fails. */
void m8_audio_init(struct m8_audio *a) {(void)a;}
int m8_audio_open(struct m8_audio *a,int v) {(void)a;(void)v;return -1;}
int m8_audio_submit(struct m8_audio *a,const void *p,size_t n) {(void)a;(void)p;(void)n;return -1;}
int m8_audio_pump(struct m8_audio *a) {(void)a;return -1;}
int m8_audio_flush(struct m8_audio *a) {(void)a;return -1;}
void m8_audio_close(struct m8_audio *a) {(void)a;}

int main(void) {
    for(unsigned pipe=0;pipe<2;++pipe) {
        int fds[2];
        assert((pipe?new_pipe(fds):socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK,0,fds))==0);
        client.phase=RUNNING;client.stats.worker=-1;client.stats.error=0;
        client.stats.ready=1;client.stats.inflight=M8_PCM_INFLIGHT_LIMIT;
        client.stats.delivered_frames=0;client.deadline=milliseconds()+1000;
        client.ipc.rx=fds[0];client.ipc.tx=-1;client.ipc.pipe=pipe;client.ipc.used=0;
        unsigned expected=0;
        for(unsigned i=0;i<M8_PCM_INFLIGHT_LIMIT;++i) {
            client.outstanding[i]=100+i;expected+=100+i;
            struct pcm_status ack={ACK,100+i,0,0};
            assert(write(fds[1],&ack,sizeof(ack))==(ssize_t)sizeof(ack));
        }
        for(unsigned i=0;i<M8_PCM_INFLIGHT_LIMIT;++i)service();
        assert(client.stats.error==0&&client.stats.inflight==0);
        assert(client.stats.delivered_frames==expected);
        for(unsigned i=0;i<M8_PCM_INFLIGHT_LIMIT;++i)assert(client.outstanding[i]==0);
        close(fds[1]);ipc_close(&client.ipc);client.phase=OFF;
    }
    printf("MEDIA_PCM_CREDITS_PASS limit=%u transports=2 hardware=0\n",(unsigned)M8_PCM_INFLIGHT_LIMIT);
}
