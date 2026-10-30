#include "OverlayBlit.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
using namespace lilsmoke::media;
using Clock=std::chrono::steady_clock;
__attribute__((noinline)) static void reference(const uint32_t *src,uint8_t *dst,size_t pitch,
                                               unsigned w,unsigned h,unsigned ow,unsigned oh){
    for(unsigned y=0;y<oh;++y){
        const auto *row=src+size_t(y)*h/oh*w;auto *out=reinterpret_cast<uint32_t*>(dst+y*pitch);
        if(w==ow)std::memcpy(out,row,ow*4);
        else for(unsigned x=0;x<ow;++x)out[x]=row[size_t(x)*w/ow];
    }
}
static void check(unsigned w,unsigned h,unsigned ow,unsigned oh){
    std::vector<uint32_t> source(w*h);for(size_t i=0;i<source.size();++i)source[i]=uint32_t(i*0x1234567u);
    const size_t pitch=(ow+13)*4,offset=pitch*3+5*4;
    std::vector<uint8_t> expected(pitch*(oh+7),0x5a),actual=expected;
    reference(source.data(),expected.data()+offset,pitch,w,h,ow,oh);
    OverlayBlit blit;blit.reset(w,h,ow,oh);blit.copy(source.data(),actual.data()+offset,pitch);
    assert(expected==actual);
    std::reverse(source.begin(),source.end());
    reference(source.data(),expected.data()+offset,pitch,w,h,ow,oh);
    blit.copy(source.data(),actual.data()+offset,pitch);assert(expected==actual);
    double before=0,after=0;
    for(unsigned run=0;run<5;++run){
        auto at=Clock::now();for(unsigned n=0;n<10;++n)reference(source.data(),expected.data()+offset,pitch,w,h,ow,oh);
        before+=std::chrono::duration<double,std::milli>(Clock::now()-at).count();
        at=Clock::now();for(unsigned n=0;n<10;++n)blit.copy(source.data(),actual.data()+offset,pitch);
        after+=std::chrono::duration<double,std::milli>(Clock::now()-at).count();assert(actual==expected);
    }
    std::printf("OVERLAY_PASS source=%ux%u destination=%ux%u old_ms=%.3f new_ms=%.3f speedup=%.3f padding_offsets_exact=1\n",w,h,ow,oh,before/50,after/50,before/after);
}
int main(){check(856,480,1280,720);check(856,480,1920,1080);check(640,480,640,480);check(856,480,320,240);check(321,241,853,479);}
