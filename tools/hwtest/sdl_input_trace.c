#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

void SDL_PumpEvents(void)
{
    static void (*next)(void);
    static Uint8 previous[SDL_NUM_SCANCODES];
    static FILE *log;
    int count;
    const Uint8 *keys;
    if (!next) next = dlsym(RTLD_NEXT, "SDL_PumpEvents");
    next();
    keys = SDL_GetKeyboardState(&count);
    if (count > SDL_NUM_SCANCODES) count = SDL_NUM_SCANCODES;
    if (memcmp(previous, keys, count) == 0) return;
    if (!log) log = fopen("/persist/qa-input.log", "a");
    if (log) {
        fprintf(log, "%u held:", SDL_GetTicks());
        for (int i = 0; i < count; ++i)
            if (keys[i]) fprintf(log, " %d=%s", i, SDL_GetScancodeName(i));
        fputc('\n', log);
        fflush(log);
    }
    memcpy(previous, keys, count);
}
