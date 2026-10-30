/* Full-system diagnostic only: validate SDL/DirectFB evdev key translation. */
#include <SDL.h>
#include <stdio.h>

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "INPUT_PROBE_ERROR %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("DirectFB input diagnostic", 0, 0,
        640, 480, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = window ?
        SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : NULL;
    if (!renderer) {
        fprintf(stderr, "INPUT_PROBE_ERROR %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    SDL_SetRenderDrawColor(renderer, 20, 130, 80, 255);
    SDL_RenderClear(renderer); SDL_RenderPresent(renderer);
    fprintf(stderr, "INPUT_PROBE_READY diagnostic_only=1\n");
    fflush(stderr);
    int done = 0;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) done = 1;
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                fprintf(stderr, "INPUT_PROBE_KEY type=%s scancode=%d name=%s\n",
                    event.type == SDL_KEYDOWN ? "down" : "up",
                    event.key.keysym.scancode,
                    SDL_GetScancodeName(event.key.keysym.scancode));
                fflush(stderr);
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F12)
                    done = 1;
            }
        }
        SDL_Delay(10);
    }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
