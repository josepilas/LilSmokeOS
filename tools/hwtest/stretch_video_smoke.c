#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t hash(const SDL_Surface *surface) {
    const unsigned char *p = surface->pixels;
    uint32_t value = 2166136261u;
    for (size_t n = 0; n < (size_t)surface->pitch * surface->h; ++n)
        value = (value ^ p[n]) * 16777619u;
    return value;
}

int main(int argc, char **argv) {
    const Uint32 formats[] = {SDL_PIXELFORMAT_INDEX8, SDL_PIXELFORMAT_RGB565,
                             SDL_PIXELFORMAT_RGB24, SDL_PIXELFORMAT_ARGB8888};
    const SDL_Rect sources[] = {{3,5,251,211},{0,0,257,223},{2,4,13,9},{1,1,1,1},
                                {3,5,251,211},{0,0,257,223},{7,9,5,3},{0,0,127,127}};
    const SDL_Rect targets[] = {{7,11,597,446},{1,3,512,446},{3,5,91,63},{13,17,17,31},
                                {9,13,127,101},{7,9,257,223},{1,3,4,2},{7,9,255,255}};
    if (argc == 2 && SDL_strcmp(argv[1],"--benchmark") == 0) {
        for (unsigned f = 1; f < 4; ++f) {
            SDL_Surface *src = SDL_CreateRGBSurfaceWithFormat(0,257,223,0,formats[f]);
            SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0,641,481,0,formats[f]);
            if (!src || !dst) return 1;
            SDL_memset(src->pixels,0x6d,(size_t)src->pitch*src->h);
            Uint64 started = SDL_GetPerformanceCounter();
            for (unsigned n = 0; n < 100; ++n)
                if (SDL_SoftStretch(src,&sources[0],dst,&targets[0]) != 0) return 2;
            double seconds = (double)(SDL_GetPerformanceCounter()-started)/SDL_GetPerformanceFrequency();
            printf("SCALE_BENCH format=%u calls=100 seconds=%.6f\n",f,seconds);
            SDL_FreeSurface(src); SDL_FreeSurface(dst);
        }
        return 0;
    }
    for (unsigned f = 0; f < 4; ++f) {
        for (unsigned test = 0; test < 8; ++test) {
            SDL_Surface *src = SDL_CreateRGBSurfaceWithFormat(0,257,223,0,formats[f]);
            SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0,641,481,0,formats[f]);
            if (!src || !dst) return 1;
            unsigned char *pixels = src->pixels;
            for (size_t n = 0; n < (size_t)src->pitch * src->h; ++n)
                pixels[n] = (unsigned char)(n * 13 + n / 17 + test * 31);
            SDL_memset(dst->pixels, 0xa5, (size_t)dst->pitch * dst->h);
            if (SDL_SoftStretch(src,&sources[test],dst,&targets[test]) != 0) {
                fprintf(stderr,"stretch failed: %s\n",SDL_GetError()); return 2;
            }
            printf("STRETCH format=%u case=%u hash=%08x\n",f,test,hash(dst));
            SDL_FreeSurface(src); SDL_FreeSurface(dst);
        }
        SDL_Surface *shared = SDL_CreateRGBSurfaceWithFormat(0,257,223,0,formats[f]);
        if (!shared) return 1;
        unsigned char *pixels = shared->pixels;
        for (size_t n = 0; n < (size_t)shared->pitch * shared->h; ++n)
            pixels[n] = (unsigned char)(n * 7 + n / 11);
        SDL_Rect from={4,6,97,63}, to={7,9,193,126};
        if (SDL_SoftStretch(shared,&from,shared,&to) != 0) return 2;
        printf("STRETCH format=%u overlap hash=%08x\n",f,hash(shared));
        SDL_FreeSurface(shared);
    }
    return 0;
}
