/* QA-only preload: captures a rendered native menu and then requests quit. */
#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned frames;
static unsigned attempts;
int od_menu_qa_nonblank(SDL_Surface *surface)
{
    unsigned different=0;
    if(!surface)return -1;
    const unsigned char *pixels=(const unsigned char *)surface->pixels;
    if(SDL_LockSurface(surface)<0)return -1;
    for(int y=0;y<surface->h;++y)
        for(int x=0;x<surface->w;++x)
            if(memcmp(pixels+y*surface->pitch+x*surface->format->BytesPerPixel,
                pixels,surface->format->BytesPerPixel))++different;
    SDL_UnlockSurface(surface);
    return (int)different;
}
static void capture(SDL_Surface *surface)
{
    if(!surface||frames)return;
    int different=od_menu_qa_nonblank(surface);
    if(different<0)exit(90);
    fprintf(stderr,"OD_MENU_QA frame=1 size=%dx%d different_pixels=%d\n",surface->w,surface->h,different);
    if(!different){if(++attempts>300)exit(91);return;}
    frames=1;
    const char *output=getenv("OD_MENU_QA_FRAME");
    if(output&&SDL_SaveBMP(surface,output)<0)exit(92);
    SDL_Event event;SDL_zero(event);event.type=SDL_QUIT;
    if(SDL_PushEvent(&event)<0)exit(93);
}
int SDL_UpdateWindowSurface(SDL_Window *window)
{
    int (*real)(SDL_Window *)=dlsym(RTLD_NEXT,"SDL_UpdateWindowSurface");
    int result=real?real(window):-1;
    capture(SDL_GetWindowSurface(window));return result;
}
void SDL_RenderPresent(SDL_Renderer *renderer)
{
    void (*real)(SDL_Renderer *)=dlsym(RTLD_NEXT,"SDL_RenderPresent");
    if(real)real(renderer);
    if(!frames) {
        int w,h;
        if(SDL_GetRendererOutputSize(renderer,&w,&h)<0)exit(94);
        SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
        if(!surface||SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch)<0)exit(95);
        capture(surface);SDL_FreeSurface(surface);
    }
}
