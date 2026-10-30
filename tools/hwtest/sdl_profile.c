#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <time.h>

static double clear_time, upload_time, copy_time, present_time;
static unsigned frames;
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int index, Uint32 flags) {
    SDL_Renderer *(*real)(SDL_Window *, int, Uint32) = dlsym(RTLD_NEXT, "SDL_CreateRenderer");
    SDL_Renderer *r = real(w, index, flags);
    SDL_RendererInfo info;
    if (r && SDL_GetRendererInfo(r, &info) == 0)
        fprintf(stderr, "PROFILE renderer=%s flags=%u window_format=%s\n", info.name, info.flags, SDL_GetPixelFormatName(SDL_GetWindowPixelFormat(w)));
    return r;
}
int SDL_RenderClear(SDL_Renderer *r) {
    int (*real)(SDL_Renderer *) = dlsym(RTLD_NEXT, "SDL_RenderClear");
    double start = now(); int result = real(r); SDL_RenderFlush(r);
    clear_time += now()-start; return result;
}
int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch) {
    int (*real)(SDL_Texture *, const SDL_Rect *, const void *, int) = dlsym(RTLD_NEXT, "SDL_UpdateTexture");
    double start = now(); int result = real(t,rect,pixels,pitch);
    upload_time += now()-start; return result;
}
int SDL_RenderCopyEx(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst, double angle, const SDL_Point *center, SDL_RendererFlip flip) {
    int (*real)(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *, double, const SDL_Point *, SDL_RendererFlip) = dlsym(RTLD_NEXT, "SDL_RenderCopyEx");
    double start = now(); int result = real(r,t,src,dst,angle,center,flip); SDL_RenderFlush(r);
    copy_time += now()-start; return result;
}
void SDL_RenderPresent(SDL_Renderer *r) {
    void (*real)(SDL_Renderer *) = dlsym(RTLD_NEXT, "SDL_RenderPresent");
    double start=now(); real(r); present_time += now()-start; frames++;
}
__attribute__((destructor)) static void report(void) {
    fprintf(stderr, "PROFILE frames=%u clear_s=%.6f upload_s=%.6f copy_s=%.6f present_s=%.6f\n", frames,clear_time,upload_time,copy_time,present_time);
}
