#include "Decoder.h"
#include "Renderer.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace lilsmoke::media;
using Clock=std::chrono::steady_clock;
static double milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
int main(int argc,char **argv){
    assert(argc==2);
    assert(SDL_Init(0)==0);
    lilsmoke::Renderer canvas;assert(canvas.initOffscreen(856,480));
    auto *renderer=static_cast<SDL_Renderer*>(canvas.sdlRenderer());
    Decoder decoder;assert(decoder.open(argv[1],Kind::Video,640,360));
    SDL_Texture *texture=nullptr;int textureWidth=0,textureHeight=0;
    uint64_t frames=0,pcm=0,hash=1469598103934665603ULL,ptsHash=1469598103934665603ULL;double pump=0,render=0,last=0;
    const auto start=Clock::now();
    for(unsigned turn=0;turn<200000&&!decoder.eof;++turn){
        auto at=Clock::now();assert(decoder.pump());pump+=milliseconds(at);
        for(size_t i=decoder.audioHead;i<decoder.audio.size();++i){hash^=uint16_t(decoder.audio[i]);hash*=1099511628211ULL;}
        pcm+=decoder.audioFrames();decoder.consumeAudio(decoder.audioFrames());
        while(!decoder.video.empty()){
            auto frame=std::move(decoder.video.front());decoder.video.pop_front();
            at=Clock::now();
#ifdef MEDIA_ARGB_PROFILE
            const bool argb=!frame.argb.empty();
            if(argb){assert(frame.pixels.empty());for(auto p:frame.argb)assert((p>>24)==255);}
            const Uint32 format=argb?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565;
            const void *pixels=argb?static_cast<const void*>(frame.argb.data()):frame.pixels.data();
            const int pitch=frame.width*(argb?4:2);
#else
            const Uint32 format=SDL_PIXELFORMAT_RGB565;const void *pixels=frame.pixels.data();const int pitch=frame.width*2;
#endif
            if(!texture||textureWidth!=frame.width||textureHeight!=frame.height){
                if(texture)SDL_DestroyTexture(texture);
                texture=SDL_CreateTexture(renderer,format,SDL_TEXTUREACCESS_STREAMING,frame.width,frame.height);assert(texture);
                textureWidth=frame.width;textureHeight=frame.height;
                assert(SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE)==0);
            }
            assert(SDL_UpdateTexture(texture,nullptr,pixels,pitch)==0);
            canvas.clear(0x000000ff);canvas.drawTexture(texture,0,0,856,480);canvas.present();render+=milliseconds(at);
            ++frames;last=frame.pts;ptsHash^=uint64_t(frame.pts*1000000+.5);ptsHash*=1099511628211ULL;
        }
    }
    assert(decoder.eof&&decoder.error.empty()&&frames>0&&pcm>0);
    const double total=milliseconds(start);
    std::printf("{\"frames\":%llu,\"pcm_frames\":%llu,\"pcm_hash\":\"%016llx\",\"pts_hash\":\"%016llx\",\"last_pts\":%.6f,\"pump_ms\":%.3f,\"render_ms\":%.3f,\"total_ms\":%.3f,\"fps\":%.3f}\n",
        (unsigned long long)frames,(unsigned long long)pcm,(unsigned long long)hash,(unsigned long long)ptsHash,last,pump,render,total,frames*1000/total);
    if(texture)SDL_DestroyTexture(texture);
}
