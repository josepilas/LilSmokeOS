#include "m8_pocketsnes_audio.h"
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
static atomic_int active,fail_open;
static atomic_uint packets,frames,nonzero;
int m8_pcm_init(unsigned rate,unsigned channels,unsigned bits)
{
    assert(rate==44100&&channels==2&&bits==16);
    if(atomic_load(&fail_open))return -1;
    atomic_store(&active,1);return 1;
}
int m8_pcm_active(void){return atomic_load(&active);}
int m8_pcm_close(void){atomic_store(&active,0);return 0;}
int m8_pcm_send(const int16_t *samples,size_t count)
{
    assert(samples&&count>0&&count<=M8_PCM_CHUNK_FRAMES);
    if(!atomic_load(&active))return -1;
    atomic_fetch_add(&packets,1);atomic_fetch_add(&frames,(unsigned)count);
    for(size_t i=0;i<count*2;++i)if(samples[i]){atomic_fetch_add(&nonzero,1);break;}
    return 1;
}
void qa_pcm_reset(void)
{
    atomic_store(&packets,0);atomic_store(&frames,0);atomic_store(&nonzero,0);
    atomic_store(&fail_open,0);
}
void qa_pcm_fail(int open_failure)
{
    atomic_store(&fail_open,open_failure);atomic_store(&active,0);
}
unsigned qa_pcm_packets(void){return atomic_load(&packets);}
unsigned qa_pcm_nonzero(void){return atomic_load(&nonzero);}
