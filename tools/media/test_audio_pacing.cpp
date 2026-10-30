#include <SDL2/SDL.h>
#include <cassert>
#include <cstdio>
static Uint64 milliseconds=0;
extern "C" Uint64 media_test_ticks(void) {return milliseconds;}
#define SDL_GetTicks64 media_test_ticks
#include "AudioSink.h"
#undef SDL_GetTicks64

using namespace lilsmoke::media;
static m8_pcm_stats state{};
static bool blocked=false;
extern "C" {
int m8_pcm_requested(void){return 1;}
int m8_pcm_init(unsigned rate,unsigned channels,unsigned bits){assert(rate==44100&&channels==2&&bits==16);state={};state.ready=1;return 1;}
int m8_pcm_send(const int16_t *pcm,size_t frames){
    assert(pcm&&frames>0&&frames<=735);
    if(blocked)return 0;
    // The hardware FIFO can ACK instantaneously, far faster than playback.
    state.submitted_frames+=frames;state.delivered_frames+=frames;return 1;
}
void m8_pcm_get_stats(m8_pcm_stats *stats){*stats=state;}
int m8_pcm_active(void){return state.ready;}
int m8_pcm_close(void){state.ready=0;return 0;}
}
int main(){
    Decoder decoder;decoder.audio.resize(44100*90*2,1234);
    AudioSink sink;assert(sink.open());
    sink.feed(decoder,100,false);
    constexpr uint64_t capacity=735*M8_PCM_INFLIGHT_LIMIT;
    assert(sink.submitted()==capacity&&!sink.empty());
    for(unsigned i=0;i<1000;++i)sink.feed(decoder,100,false);
    assert(sink.submitted()==capacity);
    milliseconds=8000;sink.feed(decoder,100,false);
    // Eight seconds without servicing is not permission to flood fifty seconds.
    assert(sink.submitted()==2*capacity);
    for(unsigned i=0;i<1000;++i){milliseconds+=10;sink.feed(decoder,50,false);}
    assert(sink.submitted()<=2*capacity+441000);
    assert(sink.submitted()>440000);
    const auto before=sink.submitted();const size_t pending=decoder.audioFrames();
    blocked=true;milliseconds+=100;sink.feed(decoder,100,false);
    assert(sink.submitted()==before&&decoder.audioFrames()==pending);
    blocked=false;milliseconds+=60000;sink.feed(decoder,100,false);
    assert(sink.submitted()<=before+capacity);
    sink.flush();sink.feed(decoder,100,false);
    const auto after=sink.submitted();
    for(unsigned i=0;i<1000;++i)sink.feed(decoder,100,false);
    assert(sink.submitted()==after&&!sink.empty());
    milliseconds+=1000;assert(sink.empty());
    sink.pause(true);sink.pause(false);sink.feed(decoder,100,false);
    const auto resumed=sink.submitted();
    for(unsigned i=0;i<1000;++i)sink.feed(decoder,100,false);
    assert(sink.submitted()==resumed);
    std::puts("MEDIA_NATIVE_PACING_PASS instant_ack=1 no_banked_stall=1 backpressure=1 flush_pause_resume=1 hardware=mock");
}
