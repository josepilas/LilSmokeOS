/* QA-only SDL2 framebuffer observer and native-key event source. */
#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>
#include <time.h>
unsigned long long od_debug_pc,od_debug_sp,od_debug_ra;
static void stop_debug(int value,siginfo_t *info,void *context){
    (void)value;(void)info;ucontext_t *u=context;
    od_debug_pc=u->uc_mcontext.pc;od_debug_sp=u->uc_mcontext.gregs[29];od_debug_ra=u->uc_mcontext.gregs[31];
    fprintf(stderr,"OD_DEBUG_PC=%llx SP=%llx RA=%llx\n",od_debug_pc,od_debug_sp,od_debug_ra);raise(SIGTRAP);
}
__attribute__((constructor)) static void prepare_debug(void){
    if(getenv("OD_GAME_QA_DEBUG")){struct sigaction a={0};a.sa_sigaction=stop_debug;a.sa_flags=SA_SIGINFO;sigaction(SIGALRM,&a,NULL);alarm(20);}
}
static Uint64 start;
static unsigned snapshots, mask;
static int gba_mode(void) { const char *s=getenv("OD_GAME_QA_ENGINE"); return s&&!strcmp(s,"mgba"); }
static unsigned elapsed(void) {
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    Uint64 now=(Uint64)ts.tv_sec*1000+ts.tv_nsec/1000000;
    if(!start)start=now; return (unsigned)(now-start);
}
void SDL_RenderPresent(SDL_Renderer *renderer) {
    void (*real)(SDL_Renderer *)=dlsym(RTLD_NEXT,"SDL_RenderPresent");
    real(renderer);
    unsigned time=elapsed();
    if((snapshots==0&&time>12000)||(snapshots==1&&time>(gba_mode()?23000:48000))) {
        int w,h; if(SDL_GetRendererOutputSize(renderer,&w,&h))exit(91);
        SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
        if(!surface||SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch))exit(92);
        char path[4096];snprintf(path,sizeof(path),"%s/%s.bmp",getenv("OD_GAME_QA_OUT"),snapshots?"after":"before");
        if(SDL_SaveBMP(surface,path))exit(93);SDL_FreeSurface(surface);++snapshots;
    }
}
int SDL_PollEvent(SDL_Event *event) {
    int (*real)(SDL_Event *)=dlsym(RTLD_NEXT,"SDL_PollEvent");
    if(!event)return real(event);
    unsigned time=elapsed();
    const unsigned schedule[]={14000,14300,16000,16400,gba_mode()?18000:40000,gba_mode()?19000:41500,gba_mode()?24000:44000,gba_mode()?24500:44500};
    const SDL_Keycode keys[]={SDLK_RETURN,SDLK_RETURN,gba_mode()?SDLK_x:SDLK_z,gba_mode()?SDLK_x:SDLK_z,SDLK_RIGHT,SDLK_RIGHT,gba_mode()?SDLK_ESCAPE:SDLK_z,gba_mode()?SDLK_ESCAPE:SDLK_z};
    for(unsigned i=0;i<8;++i)if(time>schedule[i]&&!(mask&(1u<<i))) {
        mask|=1u<<i;SDL_zero(*event);event->type=i%2?SDL_KEYUP:SDL_KEYDOWN;
        event->key.state=i%2?SDL_RELEASED:SDL_PRESSED;event->key.keysym.sym=keys[i];
        fprintf(stderr,"OD_GAME_KEY sym=%d down=%d\n",(int)keys[i],i%2==0);return 1;
    }
    if(time>(gba_mode()?34000:52000)&&!(mask&256)) {mask|=256;SDL_zero(*event);event->type=SDL_QUIT;return 1;}
    return real(event);
}
int SDL_WaitEventTimeout(SDL_Event *event,int timeout) {
    Uint32 begin=SDL_GetTicks();
    do {
        if(SDL_PollEvent(event))return 1;
        if(timeout==0)return 0;
        SDL_Delay(2);
    } while(timeout<0||(Uint32)(SDL_GetTicks()-begin)<(Uint32)timeout);
    return 0;
}
