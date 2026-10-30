#define main lilsmoke_media_program_main
#include "../../package/lilsmoke-media/src/Player.cpp"
#undef main
#include <cassert>
#include <cmath>

int main(int argc,char **argv){
    assert(argc==2);setenv("SDL_AUDIODRIVER","dummy",1);unsetenv("LILSMOKE_M8_AUDIO");
    Renderer r;assert(r.initOffscreen(856,480));
    Font f;assert(f.load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",16));
    Player p(r,f);p.list.files={argv[1]};assert(p.load());
    assert(p.softwareVideo("sync-test"));assert(p.sound.open());
    p.started=now();std::vector<double> skews;unsigned shown=0;
    const double begin=now();
    while(now()-begin<3){
        p.update();assert(p.error.empty());
        if(p.dirty){p.render();p.dirty=false;}
        if(p.decoded!=shown){shown=p.decoded;skews.push_back(std::abs(p.position()-p.picture.pts));}
        SDL_Delay(2);
    }
    assert(shown>10&&p.sound.submitted()>44100&&!skews.empty());
    std::sort(skews.begin(),skews.end());
    const double median=skews[skews.size()/2],p95=skews[(skews.size()-1)*95/100];
    assert(median<.2&&p95<1);
    p.pause(true);const auto pausedAt=p.position();SDL_Delay(50);p.update();assert(p.position()==pausedAt);
    p.seek(.5);assert(p.error.empty()&&p.paused&&p.base==.5);p.pause(false);
    const auto until=now()+.6;
    while(now()<until){p.update();assert(p.error.empty());SDL_Delay(2);}
    assert(!p.paused&&p.position()>=.5);
    std::printf("VIDEO_SYNC_PASS shown=%u ref=%llu converted=%llu drop_convert=%llu drop_present=%u median_abs_est_ms=%.3f p95_abs_est_ms=%.3f pcm_frames=%llu pause_seek_resume=1 clock=dummy-not-DAC\n",
        shown,(unsigned long long)p.decoder.profile.received,(unsigned long long)p.decoder.profile.converted,
        (unsigned long long)p.decoder.discardedVideoFrames,p.dropped,median*1000,p95*1000,(unsigned long long)p.sound.submitted());
}
