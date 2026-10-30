#include "Decoder.h"
#include "PlaybackClock.h"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace lilsmoke::media;

int main(int argc,char **argv) {
    PlaybackClock clock;
    assert(clock.played(0,500)==0);
    assert(clock.played(44100,1000)==0);
    assert(clock.played(44100,1500)==22050);
    assert(clock.played(44100,2500)==44100);
    assert(clock.played(0,3000)==0);
    clock.reset();
    const uint64_t wrap=uint64_t(std::numeric_limits<uint32_t>::max());
    assert(clock.played(88200,wrap)==0);
    assert(clock.played(88200,wrap+1000)==44100);
    assert(clock.played(88200,wrap-1)==44100);
    clock.reset();
    assert(clock.played(0,wrap+2000)==0);
    assert(clock.played(100,wrap+2000)==0);
    assert(clock.played(100,wrap+3000)==100);
    assert(clock.played(100,wrap+60000)==100);
    assert(clock.played(44100,wrap+60000)==100);
    assert(clock.played(44100,wrap+60010)==541);
    unsigned files=0;
    for(int arg=1;arg<argc;++arg) {
        Decoder decoder;
        assert(decoder.open(argv[arg],kind(argv[arg]),640,360));
        assert(decoder.hasAudio);
        // Deliberately leave the first video frames queued. They cannot block
        // demuxing the audio which drives the production playback clock.
        for(unsigned i=0;i<256&&!decoder.audioFrames();++i) {
            assert(decoder.pump());
            assert(decoder.video.size()<=8);
        }
        assert(decoder.audioFrames()>0);
        uint64_t frames=0;unsigned pictures=0,turns=0;
        for(;turns<200000&&!decoder.eof;++turns) {
            frames+=decoder.audioFrames();decoder.consumeAudio(decoder.audioFrames());
            pictures+=unsigned(decoder.video.size());decoder.video.clear();
            assert(decoder.pump());
        }
        frames+=decoder.audioFrames();pictures+=unsigned(decoder.video.size());
        assert(decoder.eof&&frames>44100);
        if(decoder.hasVideo)assert(pictures>5);
        assert(decoder.seek(0));
        assert(!decoder.eof&&decoder.audioFrames()==0&&decoder.video.empty());
        for(unsigned i=0;i<256&&!decoder.audioFrames();++i)assert(decoder.pump());
        assert(decoder.audioFrames()>0);
        assert(decoder.seek(1));
        for(unsigned i=0;i<256&&!decoder.audioFrames();++i) {
            decoder.video.clear();assert(decoder.pump());
        }
        assert(decoder.audioFrames()>0);
        std::printf("MEDIA_RECOVERY file=%u pcm_frames=%llu pictures=%u pumps=%u eof=1 seek=1\n",
            ++files,(unsigned long long)frames,pictures,turns);
        if(decoder.hasVideo) {
            Decoder external;
            assert(external.open(argv[arg],kind(argv[arg]),640,360,true));
            assert(external.hasVideo&&external.hasAudio);
            uint64_t pcm=0;unsigned iterations=0;
            for(;iterations<200000&&!external.eof;++iterations) {
                pcm+=external.audioFrames();external.consumeAudio(external.audioFrames());
                assert(external.pump()&&external.video.empty());
            }
            pcm+=external.audioFrames();
            assert(external.eof&&pcm==frames);
            assert(external.seek(1));
            for(unsigned i=0;i<256&&!external.audioFrames();++i)assert(external.pump());
            assert(external.audioFrames()>0&&external.video.empty());
            std::printf("MEDIA_EXTERNAL_VIDEO_PASS file=%u pcm_frames=%llu seek=1 no_software_frames=1\n",
                files,(unsigned long long)pcm);
        }
    }
    assert(files>0);
    std::printf("MEDIA_RECOVERY_PASS files=%u clock_checks=14\n",files);
}
