#include "NativeVideo.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <sys/wait.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

using lilsmoke::media::NativeVideo;
template<class Predicate> void until(NativeVideo &video, Predicate predicate) {
    const auto deadline=SDL_GetTicks64()+3000;
    while(!predicate()&&SDL_GetTicks64()<deadline){video.update();SDL_Delay(5);}
    assert(predicate());
}
int main(){
    assert(SDL_Init(SDL_INIT_TIMER)==0);
    assert(setenv("LD_PRELOAD","must-not-reach-firmware",1)==0);
    assert(setenv("LD_LIBRARY_PATH","/must-not-reach-firmware",1)==0);
    assert(setenv("LD_AUDIT","must-not-reach-firmware",1)==0);
    {
        NativeVideo video;
        assert(video.start("/valid",320,240));
        until(video,[&]{return video.ready();});
        assert(video.error().empty()&&video.duration()==30);
        video.play();until(video,[&]{return video.position()>=.4;});
        video.pause(true);SDL_Delay(100);video.update();
        const auto at=video.position();SDL_Delay(150);video.update();
        assert(video.position()==at);
        assert(!video.seek(-1)&&!video.seek(INFINITY)&&video.seek(3));
        until(video,[&]{return video.position()==3;});
        video.pause(false);until(video,[&]{return video.position()>3;});
        video.aspect(640,480,true);
        const auto begin=SDL_GetTicks64();video.stop();
        assert(SDL_GetTicks64()-begin<1000);
        assert(video.start("/unknown",320,240));
        until(video,[&]{return video.ready()&&video.duration()==30;});
        assert(video.error().empty());video.play();
        until(video,[&]{return video.position()>0;});assert(video.stop());
        assert(video.start("/negative",320,240));
        until(video,[&]{return !video.error().empty();});assert(video.stop());
        assert(video.start("/device-error",320,240));
        until(video,[&]{return !video.error().empty();});
        assert(video.error().find("viddec-open errno=12")!=std::string::npos);assert(video.stop());
        assert(video.start("/malformed",320,240));
        until(video,[&]{return !video.error().empty();});video.stop();
        assert(video.start("/overflow",320,240));
        until(video,[&]{return !video.error().empty();});video.stop();
        assert(video.start("/exit",320,240));
        until(video,[&]{return !video.error().empty();});video.stop();
        assert(video.start("/stuck",320,240));
        until(video,[&]{return video.ready();});
        const auto shutdown=SDL_GetTicks64();video.stop();
        assert(SDL_GetTicks64()-shutdown<1000);
    }
    int status;assert(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD);
    assert(!std::strcmp(std::getenv("LD_PRELOAD"),"must-not-reach-firmware"));
    assert(!std::strcmp(std::getenv("LD_LIBRARY_PATH"),"/must-not-reach-firmware"));
    assert(!std::strcmp(std::getenv("LD_AUDIT"),"must-not-reach-firmware"));
    unsetenv("LD_PRELOAD");unsetenv("LD_LIBRARY_PATH");unsetenv("LD_AUDIT");
    SDL_Quit();
    std::puts("PASS native controller IPC: ready/play/pause/seek/aspect/stop, malformed/overflow/exit/kill/reap");
}
