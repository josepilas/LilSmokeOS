#define _GNU_SOURCE
#include "od-sdl.h"
#include <SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SDL's software renderer draws into a small CPU surface. Only the validated
 * hcfb presenter may scale it to HDMI; SDL never repaints the TV-sized desktop. */
typedef struct {
    SDL_Window *window;
    SDL_Surface *surface;
    SDL_Renderer *renderer;
    int width,height;
} native_window;
static native_window windows[4];
typedef int (*present_fn)(const void *,unsigned,unsigned,unsigned,int,unsigned,unsigned);
static present_fn presenter;

static int active(void)
{
    if(od_api()!=2||!od_enabled())return 0;
    if(!presenter)presenter=(present_fn)dlsym(RTLD_DEFAULT,"lilsmoke_present_rgb");
    return presenter!=NULL;
}
static native_window *find_window(SDL_Window *window)
{
    for(unsigned i=0;i<4;++i)if(windows[i].window==window)return &windows[i];
    return NULL;
}
static native_window *find_renderer(SDL_Renderer *renderer)
{
    if(!renderer)return NULL;
    for(unsigned i=0;i<4;++i)if(windows[i].renderer==renderer)return &windows[i];
    return NULL;
}
static void size_for_engine(int *w,int *h)
{
    const char *engine=getenv("LILSMOKE_OD_ENGINE");
    if(!getenv("LILSMOKE_NATIVE_PICKER")&&engine) {
        if(!strcmp(engine,"mgba")){*w=240;*h=160;}
        else if(!strcmp(engine,"easyrpg")){*w=320;*h=240;}
    }
    while(*w>640||*h>480){*w=(*w+1)/2;*h=(*h+1)/2;}
    if(*w<1)*w=320;
    if(*h<1)*h=240;
}
static int resize_surface(native_window *v,int w,int h)
{
    if(w<1||h<1||w>640||h>480){od_error("Native video resolution exceeds 640x480");return -1;}
    if(v->surface) {
        int (*flush)(SDL_Renderer *)=od_real("SDL_RenderFlush",2);
        int (*clip)(SDL_Surface *,const SDL_Rect *)=od_real("SDL_SetClipRect",2);
        int (*viewport)(SDL_Renderer *,const SDL_Rect *)=od_real("SDL_RenderSetViewport",2);
        if(v->renderer&&flush)flush(v->renderer);
        /* The allocation/pitch stay fixed; queued draws are flushed before the
         * active rectangle changes. No renderer or engine texture is destroyed. */
        v->surface->w=w;v->surface->h=h;
        if(clip)clip(v->surface,NULL);
        if(v->renderer&&viewport)viewport(v->renderer,NULL);
    }
    v->width=w;v->height=h;od_input_viewport(w,h);
    return 0;
}
static SDL_Surface *surface_for(native_window *v)
{
    if(!v->surface) {
        SDL_Surface *(*create)(Uint32,int,int,int,Uint32)=od_real("SDL_CreateRGBSurfaceWithFormat",2);
        v->surface=create?create(0,640,480,16,SDL_PIXELFORMAT_RGB565):NULL;
        if(v->surface)resize_surface(v,v->width,v->height);
    }
    return v->surface;
}
SDL_Window *SDL_CreateWindow(const char *title,int x,int y,int w,int h,Uint32 flags)
{
    SDL_Window *(*real)(const char *,int,int,int,int,Uint32)=od_real("SDL_CreateWindow",2);
    if(!active())return real?real(title,x,y,w,h,flags):NULL;
    native_window *v=NULL;
    for(unsigned i=0;i<4;++i)if(!windows[i].window){v=&windows[i];break;}
    if(!v){od_error("Too many native windows");return NULL;}
    size_for_engine(&w,&h);
    flags&=~(SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_Window *window=real?real(title,x,y,w,h,flags):NULL;
    if(window){v->window=window;v->width=w;v->height=h;od_input_viewport(w,h);}
    return window;
}
SDL_Surface *SDL_GetWindowSurface(SDL_Window *window)
{
    native_window *v=active()?find_window(window):NULL;
    SDL_Surface *(*real)(SDL_Window *)=od_real("SDL_GetWindowSurface",2);
    return v?surface_for(v):(real?real(window):NULL);
}
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window,int index,Uint32 flags)
{
    native_window *v=active()?find_window(window):NULL;
    SDL_Renderer *(*real)(SDL_Window *,int,Uint32)=od_real("SDL_CreateRenderer",2);
    if(!v)return real?real(window,index,flags):NULL;
    if(v->renderer){od_error("Native window already has a renderer");return NULL;}
    SDL_Surface *surface=surface_for(v);
    SDL_Renderer *(*create)(SDL_Surface *)=od_real("SDL_CreateSoftwareRenderer",2);
    if(surface&&create)v->renderer=create(surface);
    fprintf(stderr,"OD_SDL_VIDEO renderer=software source=%dx%d desktop_scaling=0 vsync_wait=0\n",v->width,v->height);
    return v->renderer;
}
SDL_Renderer *SDL_GetRenderer(SDL_Window *window)
{
    native_window *v=active()?find_window(window):NULL;
    SDL_Renderer *(*real)(SDL_Window *)=od_real("SDL_GetRenderer",2);
    return v?v->renderer:(real?real(window):NULL);
}
int SDL_CreateWindowAndRenderer(int w,int h,Uint32 flags,SDL_Window **window,SDL_Renderer **renderer)
{
    int (*real)(int,int,Uint32,SDL_Window **,SDL_Renderer **)=od_real("SDL_CreateWindowAndRenderer",2);
    if(!active())return real?real(w,h,flags,window,renderer):-1;
    if(!window||!renderer){od_error("Missing native window/renderer output");return -1;}
    *window=SDL_CreateWindow("",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,w,h,flags);
    *renderer=*window?SDL_CreateRenderer(*window,-1,SDL_RENDERER_SOFTWARE):NULL;
    if(!*renderer){if(*window)SDL_DestroyWindow(*window);*window=NULL;return -1;}
    return 0;
}
int SDL_RenderSetVSync(SDL_Renderer *renderer,int enabled)
{
    native_window *v=active()?find_renderer(renderer):NULL;
    int (*real)(SDL_Renderer *,int)=od_real("SDL_RenderSetVSync",2);
    /* Engine/audio pacing already owns time. SDL's synthetic software vsync
     * would add a second delay despite there being no SDL scanout window. */
    return real?real(renderer,v?0:enabled):-1;
}
int od_video_logical(void *renderer,int w,int h)
{
    native_window *v=active()?find_renderer(renderer):NULL;
    return v&&w&&h?resize_surface(v,w,h):0;
}
static int present(native_window *v)
{
    if(!v->surface)return -1;
    unsigned num=v->width,den=v->height;
    const char *engine=getenv("LILSMOKE_OD_ENGINE");
    if(engine&&!strcmp(engine,"prboom")){num=4;den=3;}
    if(!presenter(v->surface->pixels,v->surface->w,v->surface->h,v->surface->pitch,0,num,den)) {
        od_error("Native hcfb presentation failed; refusing desktop-sized fallback");return -1;
    }
    void (*checkpoint)(void)=dlsym(RTLD_DEFAULT,"lilsmoke_present_checkpoint");
    if(checkpoint)checkpoint();
    return 0;
}
void SDL_RenderPresent(SDL_Renderer *renderer)
{
    native_window *v=active()?find_renderer(renderer):NULL;
    void (*real)(SDL_Renderer *)=od_real("SDL_RenderPresent",2);
    if(real)real(renderer);
    if(v)present(v);
}
int SDL_UpdateWindowSurface(SDL_Window *window)
{
    native_window *v=active()?find_window(window):NULL;
    int (*real)(SDL_Window *)=od_real("SDL_UpdateWindowSurface",2);
    return v?present(v):(real?real(window):-1);
}
int SDL_UpdateWindowSurfaceRects(SDL_Window *window,const SDL_Rect *rects,int count)
{
    native_window *v=active()?find_window(window):NULL;
    int (*real)(SDL_Window *,const SDL_Rect *,int)=od_real("SDL_UpdateWindowSurfaceRects",2);
    return v?present(v):(real?real(window,rects,count):-1);
}
void SDL_GetWindowSize(SDL_Window *window,int *w,int *h)
{
    native_window *v=active()?find_window(window):NULL;
    void (*real)(SDL_Window *,int *,int *)=od_real("SDL_GetWindowSize",2);
    if(v){if(w)*w=v->width;if(h)*h=v->height;}else if(real)real(window,w,h);
}
void SDL_SetWindowSize(SDL_Window *window,int w,int h)
{
    native_window *v=active()?find_window(window):NULL;
    void (*real)(SDL_Window *,int,int)=od_real("SDL_SetWindowSize",2);
    if(v){size_for_engine(&w,&h);resize_surface(v,w,h);}
    if(real)real(window,w,h);
}
int SDL_SetWindowFullscreen(SDL_Window *window,Uint32 flags)
{
    native_window *v=active()?find_window(window):NULL;
    int (*real)(SDL_Window *,Uint32)=od_real("SDL_SetWindowFullscreen",2);
    return v?0:(real?real(window,flags):-1);
}
void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    native_window *v=find_renderer(renderer);
    void (*real)(SDL_Renderer *)=od_real("SDL_DestroyRenderer",2);
    if(real)real(renderer);
    if(v)v->renderer=NULL;
}
void SDL_DestroyWindow(SDL_Window *window)
{
    native_window *v=find_window(window);
    void (*real)(SDL_Window *)=od_real("SDL_DestroyWindow",2);
    if(v) {
        if(v->renderer)SDL_DestroyRenderer(v->renderer);
        void (*free_surface)(SDL_Surface *)=od_real("SDL_FreeSurface",2);
        if(v->surface&&free_surface)free_surface(v->surface);
        memset(v,0,sizeof(*v));
        void (*reset)(void)=dlsym(RTLD_DEFAULT,"lilsmoke_present_reset");
        if(reset)reset();
    }
    if(real)real(window);
}
void od_video_shutdown(void)
{
    for(unsigned i=0;i<4;++i)if(windows[i].window)SDL_DestroyWindow(windows[i].window);
}
