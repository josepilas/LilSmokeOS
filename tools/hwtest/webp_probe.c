#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <stdio.h>

int main(int argc, char **argv)
{
    SDL_Surface *surface;
    int initialized;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <image.webp>\n", argv[0]);
        return 2;
    }

    if (SDL_Init(0) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 3;
    }

    initialized = IMG_Init(IMG_INIT_WEBP);
    if ((initialized & IMG_INIT_WEBP) == 0) {
        fprintf(stderr, "IMG_Init WEBP failed: %s\n", IMG_GetError());
        SDL_Quit();
        return 4;
    }

    surface = IMG_Load(argv[1]);
    if (surface == NULL) {
        fprintf(stderr, "IMG_Load failed: %s\n", IMG_GetError());
        IMG_Quit();
        SDL_Quit();
        return 5;
    }

    printf("WEBP_PROBE_OK width=%d height=%d format=%s\n",
           surface->w, surface->h,
           SDL_GetPixelFormatName(surface->format->format));
    SDL_FreeSurface(surface);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
