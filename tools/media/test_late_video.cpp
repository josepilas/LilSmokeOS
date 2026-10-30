#include "Decoder.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace lilsmoke::media;
struct Result {uint64_t audio=0,hash=1469598103934665603ULL,pictures=0,discarded=0;};
static Result decode(const char *file,double clock) {
    Decoder d;assert(d.open(file,Kind::Video,640,360));
    d.setVideoClock(clock);Result r;
    for(unsigned turn=0;turn<200000&&!d.eof;++turn){
        assert(d.pump()&&d.error.empty());
        for(size_t i=d.audioHead;i<d.audio.size();++i){r.hash^=uint16_t(d.audio[i]);r.hash*=1099511628211ULL;}
        r.audio+=d.audioFrames();d.consumeAudio(d.audioFrames());
        for(const auto &frame:d.video){
            if(clock>=0)assert(frame.pts>=clock-.081);
            ++r.pictures;
        }
        d.video.clear();
    }
    assert(d.eof&&r.audio>44100);r.discarded=d.discardedVideoFrames;
    assert(d.seek(0));
    for(unsigned turn=0;turn<256&&d.video.empty();++turn){assert(d.pump());d.consumeAudio(d.audioFrames());}
    assert(!d.video.empty()&&d.video.front().pts<.1);
    return r;
}
int main(int argc,char **argv){
    assert(argc>=2);
    for(int i=1;i<argc;++i){
        auto all=decode(argv[i],-1),late=decode(argv[i],2);
        auto invalid=decode(argv[i],std::numeric_limits<double>::quiet_NaN());
        assert(all.audio==late.audio&&all.hash==late.hash);
        assert(all.pictures==late.pictures+late.discarded&&late.discarded>0&&late.pictures>0);
        assert(invalid.audio==all.audio&&invalid.hash==all.hash&&invalid.pictures==all.pictures&&!invalid.discarded);
        std::printf("LATE_VIDEO_PASS frames=%llu converted=%llu discarded=%llu pcm_frames=%llu pcm_hash=%llx seek_reset=1\n",
            (unsigned long long)all.pictures,(unsigned long long)late.pictures,(unsigned long long)late.discarded,
            (unsigned long long)all.audio,(unsigned long long)all.hash);
    }
}
