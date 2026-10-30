#define main lilsmoke_media_program_main
#include "../../package/lilsmoke-media/src/Player.cpp"
#undef main
#include <cassert>
#include <sys/wait.h>

int main(int argc,char **argv){
    assert(argc==2);
    assert(access("/usr/lib/lilsmoke/hcplayer/ld.so.1",F_OK)!=0);
    setenv("SDL_AUDIODRIVER","dummy",1);unsetenv("LILSMOKE_M8_AUDIO");
    Renderer r;assert(r.initOffscreen(320,240));
    Font f;assert(f.load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",16));
    NativeVideo native;
    {
        Player p(r,f,&native);p.list.files={argv[1]};assert(p.load());
        const auto deadline=SDL_GetTicks64()+6000;
        while(SDL_GetTicks64()<deadline&&(p.hardware||p.decoded<5||p.sound.submitted()<44100)){
            p.update();assert(p.error.empty());SDL_Delay(5);
        }
        assert(!p.hardware&&p.decoded>=5&&p.sound.submitted()>=44100);
        p.render();std::vector<uint32_t> pixels(320*240);
        assert(!SDL_RenderReadPixels(static_cast<SDL_Renderer*>(r.sdlRenderer()),nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),320*4));
        assert(std::any_of(pixels.begin(),pixels.end(),[](uint32_t v){return (v&0xffffff)>0x202020;}));
        p.pause(true);const auto at=p.position();SDL_Delay(40);p.update();assert(p.paused&&p.position()==at);
        p.pause(false);p.seek(.5);assert(p.error.empty());
        p.action(Action::Options);assert(p.panel==1);p.action(Action::Quit);assert(p.quit);
        std::printf("PLAYER_FALLBACK_PASS pictures=%u pcm=%llu pause_seek_options_exit=1\n",p.decoded,(unsigned long long)p.sound.submitted());
        p.sound.close();
    }
    assert(native.stop());int status;
    assert(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD);
}
