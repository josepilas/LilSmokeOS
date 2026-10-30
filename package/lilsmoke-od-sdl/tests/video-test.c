#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
static int enabled=1,frames,expected_w,expected_h;
static uint32_t expected_color;
int od_api(void){return 2;}
int od_enabled(void){return enabled;}
void *od_real(const char *name,int api)
{
    static void *handle;
    assert(api==2);
    if(!handle)handle=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL);
    assert(handle);return dlsym(handle,name);
}
void od_error(const char *message){fprintf(stderr,"test video error: %s\n",message);}
void od_input_viewport(int w,int h){assert(w>0&&h>0&&w<=640&&h<=480);}
int lilsmoke_present_rgb(const void *pixels,unsigned w,unsigned h,unsigned pitch,
                        int rgb32,unsigned num,unsigned den)
{
    assert((int)w==expected_w&&(int)h==expected_h&&!rgb32&&num&&den);
    assert(w<=640&&h<=480&&pitch>=w*2);
    uint16_t expected=((expected_color>>8)&0xf800)|((expected_color>>5)&0x7e0)|((expected_color>>3)&0x1f);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        uint16_t pixel=*(const uint16_t *)((const uint8_t *)pixels+y*pitch+x*2);
        assert(pixel==expected);
    }
    ++frames;return 1;
}
void lilsmoke_present_reset(void){}
#include "../src/video.c"
static void draw(SDL_Renderer *r,int w,int h,uint32_t color)
{
    expected_w=w;expected_h=h;expected_color=color;
    assert(!SDL_SetRenderDrawColor(r,color>>16,color>>8,color,255));
    assert(!SDL_RenderClear(r));SDL_RenderPresent(r);
}
int main(void)
{
    assert(!SDL_Init(SDL_INIT_VIDEO));
    setenv("LILSMOKE_OD_ENGINE","mgba",1);
    SDL_Window *w=SDL_CreateWindow("native",0,0,960,640,SDL_WINDOW_FULLSCREEN_DESKTOP);
    assert(w);int x,y;SDL_GetWindowSize(w,&x,&y);assert(x==240&&y==160);
    SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    assert(r&&SDL_GetRenderer(w)==r);
    assert(!SDL_RenderSetVSync(r,1));
    SDL_RendererInfo info;assert(!SDL_GetRendererInfo(r,&info));assert(info.flags&SDL_RENDERER_SOFTWARE);
    assert(!(info.flags&SDL_RENDERER_PRESENTVSYNC));
    draw(r,240,160,0xff0000);
    assert(!od_video_logical(r,320,240));
    int (*logical)(SDL_Renderer *,int,int)=od_real("SDL_RenderSetLogicalSize",2);
    assert(!logical(r,320,240));draw(r,320,240,0x00ff00);
    assert(!od_video_logical(r,240,160));assert(!logical(r,240,160));
    SDL_Texture *t=SDL_CreateTexture(r,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,240,160);
    assert(t);uint32_t blue[240*160];for(unsigned i=0;i<240*160;++i)blue[i]=0xffff0000;
    assert(!SDL_UpdateTexture(t,NULL,blue,240*4));
    assert(!SDL_RenderCopy(r,t,NULL,NULL));expected_color=0x0000ff;expected_w=240;expected_h=160;
    SDL_RenderPresent(r);SDL_DestroyTexture(t);
    assert(od_video_logical(r,641,480)<0);
    SDL_DestroyWindow(w);
    setenv("LILSMOKE_NATIVE_PICKER","1",1);
    w=SDL_CreateWindow("picker",0,0,320,240,0);assert(w);
    SDL_Surface *s=SDL_GetWindowSurface(w);assert(s&&s->w==320&&s->h==240);
    SDL_FillRect(s,NULL,SDL_MapRGB(s->format,255,255,255));
    expected_color=0xffffff;expected_w=320;expected_h=240;
    assert(!SDL_UpdateWindowSurface(w));
    SDL_SetWindowSize(w,426,240);s=SDL_GetWindowSurface(w);assert(s->w==426&&s->h==240);
    SDL_FillRect(s,NULL,SDL_MapRGB(s->format,255,255,255));expected_w=426;
    assert(!SDL_UpdateWindowSurfaceRects(w,NULL,0));
    od_video_shutdown();assert(!find_window(w));
    assert(!SDL_CreateWindowAndRenderer(80,60,0,&w,&r));
    draw(r,80,60,0xffffff);SDL_DestroyWindow(w);
    enabled=0;w=SDL_CreateWindow("generic",0,0,80,60,0);assert(w);
    SDL_GetWindowSize(w,&x,&y);assert(x==80&&y==60);SDL_DestroyWindow(w);
    SDL_Quit();assert(frames==6);
    puts("PASS: native SDL2 software frames, colors, all edges, logical resize, texture input, surface menus, lifecycle and generic passthrough (mock presenter).");
    return 0;
}
