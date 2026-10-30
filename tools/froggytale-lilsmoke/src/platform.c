/* SPDX-License-Identifier: AGPL-3.0-only */
#include "port.h"
#include "sw_renderer.h"
#include "gettime.h"
#include <dlfcn.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

unsigned ft_frames;
uint32_t ft_frame_hash;
uint16_t ft_pixels[320*240];
volatile sig_atomic_t ft_stop;
static SDL_Window* window;
static Runner* active_runner;
static void* display;
static int (*present)(const void*,unsigned,unsigned,unsigned,int,unsigned,unsigned);
static bool offline;

static void stop(int sig) { (void)sig; ft_stop=1; }
void platformLog(logType type,const char* fmt,va_list args) {
    FILE* f=type==LOG_TYPE_NORMAL?stdout:stderr;vfprintf(f,fmt,args);
}
bool ft_platform_open(bool headless) {
    offline=headless; ft_stop=0;
    signal(SIGTERM,stop);signal(SIGINT,stop);
    const char* native=getenv("LILSMOKE_HCFB");
    if(!headless && native && strcmp(native,"1")==0) {
        display=dlopen("/os/libm8-display.so",RTLD_NOW|RTLD_GLOBAL);
        if(!display) {fprintf(stderr,"M8 platform display unavailable: %s\n",dlerror());return false;}
        present=(int(*)(const void*,unsigned,unsigned,unsigned,int,unsigned,unsigned))dlsym(display,"lilsmoke_present_rgb");
        if(!present) {fprintf(stderr,"M8 presenter ABI unavailable\n");dlclose(display);display=NULL;return false;}
    }
    unsigned flags=SDL_INIT_TIMER|SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER;
    if(!headless && !present) flags|=SDL_INIT_VIDEO;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
    if(SDL_Init(flags)<0) {fprintf(stderr,"SDL init: %s\n",SDL_GetError());ft_platform_close();return false;}
    if(!headless && !present) {
        window=SDL_CreateWindow("Undertale-LilSmoke 0.1.0 experimental",SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,320,240,0);
        if(!window) {fprintf(stderr,"Software window: %s\n",SDL_GetError());ft_platform_close();return false;}
    }
    SDL_ShowCursor(SDL_DISABLE);
    ft_input_init();return true;
}
void ft_platform_close(void) {
    ft_input_close();
    if(window) {SDL_DestroyWindow(window);window=NULL;}
    SDL_Quit();
    /* The platform presenter owns process-lifetime stock handles. */
    if(display) {dlclose(display);display=NULL;}
    present=NULL;active_runner=NULL;
}
void ft_frame(const void* data,unsigned w,unsigned h,unsigned pitch_pixels) {
    if(!data || w!=320 || h!=240 || pitch_pixels<w || pitch_pixels>640) {
        fprintf(stderr,"Invalid software framebuffer\n");ft_stop=1;return;
    }
    uint32_t hash=2166136261u;
    for(unsigned y=0;y<h;++y) {
        const uint16_t* row=(const uint16_t*)data+y*pitch_pixels;
        memcpy(ft_pixels+y*w,row,w*2);
        for(unsigned x=0;x<w;++x) {hash=(hash^(row[x]&255))*16777619u;hash=(hash^(row[x]>>8))*16777619u;}
    }
    ft_frame_hash=hash;++ft_frames;
    const char* captures=getenv("FROGGY_CAPTURE_DIR");
    if(captures && (ft_frames==1 || ft_frames==6 || ft_frames==30 || ft_frames==60 || ft_frames==120 || ft_frames==300 || ft_frames==600 || ft_frames==900)) {
        char path[700];snprintf(path,sizeof path,"%s/frame-%04u.ppm",captures,ft_frames);
        FILE* f=fopen(path,"wb");
        if(f) {
            fprintf(f,"P6\n320 240\n255\n");
            unsigned char line[320*3];
            for(unsigned y=0;y<240;++y) {
                for(unsigned x=0;x<320;++x) {uint16_t p=ft_pixels[y*320+x];line[x*3]=((p>>11)&31)*255/31;line[x*3+1]=((p>>5)&63)*255/63;line[x*3+2]=(p&31)*255/31;}
                if(fwrite(line,1,sizeof line,f)!=sizeof line)break;
            }
            fclose(f);
        }
    }
    if(offline) return;
    if(present) {
        if(!present(ft_pixels,w,h,w*2,0,4,3)) {fprintf(stderr,"Native display rejected frame\n");ft_stop=1;}
    } else if(window) {
        SDL_Surface* src=SDL_CreateRGBSurfaceWithFormatFrom(ft_pixels,w,h,16,w*2,SDL_PIXELFORMAT_RGB565);
        SDL_Surface* dst=SDL_GetWindowSurface(window);
        if(!src || !dst || SDL_BlitScaled(src,NULL,dst,NULL)<0 || SDL_UpdateWindowSurface(window)<0) ft_stop=1;
        if(src) SDL_FreeSurface(src);
    }
}
void sw_renderer_present_sink(const void* p,unsigned w,unsigned h,unsigned pitch) {ft_frame(p,w,h,pitch);}
bool platformInit(int32_t w,int32_t h,const char* title,bool headless) {
    (void)w;(void)h;(void)title;return ft_platform_open(headless);
}
void platformInitFunctions(Runner* r) {active_runner=r;}
void platformExit(void) {ft_platform_close();}
bool platformHandleEvents(void) {
    bool quit=ft_input_poll();
    if(active_runner) {ft_input_runner(active_runner);if(quit) active_runner->shouldExit=true;}
    return quit;
}
void platformSwapBuffers(void) {SwRenderer_present();}
void* platformGetProcAddress(const char* name) {(void)name;return NULL;}
bool platformGetWindowSize(int32_t* w,int32_t* h) {if(w)*w=320;if(h)*h=240;return true;}
bool platformGetScaledWindowSize(int32_t* w,int32_t* h) {return platformGetWindowSize(w,h);}
void platformSetWindowSize(int32_t w,int32_t h) {(void)w;(void)h;}
void platformSetWindowTitle(const char* s) {if(window) SDL_SetWindowTitle(window,s);}
void platformGetMousePos(double* x,double* y) {if(x)*x=160;if(y)*y=120;}
void platformSleepUntil(uint64_t target) {
    while(!ft_stop) {
        uint64_t now=nowNanos();if(now>=target) break;
        uint64_t delay=target-now;if(delay>10000000) delay=10000000;
        struct timespec ts={(time_t)(delay/1000000000),(long)(delay%1000000000)};
        while(nanosleep(&ts,&ts)<0 && errno==EINTR && !ft_stop) {}
    }
}
