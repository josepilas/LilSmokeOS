/* Test-only SDL 1.2 observer. Never installed with an emulator. */
#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static double started;
static int captured, sent_quit;
static SDL_Surface *observed;
struct action { double at, until; int key, down, up; };
static struct action actions[32];
static int action_count, actions_ready;
static double setting(const char *name, double fallback) {
    const char *value = getenv(name);
    return value ? atof(value) : fallback;
}
static void setup_actions(void) {
    char *copy, *part, *save;
    const char *value = getenv("OD_QA_KEYS");
    if (actions_ready) return;
    actions_ready = 1;
    copy = strdup(value ? value : "3:274:0.2");
    for (part = strtok_r(copy, ",", &save); part && action_count < 32; part = strtok_r(NULL, ",", &save)) {
        double at, duration;
        int key;
        if (sscanf(part, "%lf:%d:%lf", &at, &key, &duration) == 3) {
            actions[action_count++] = (struct action){at, at + duration, key, 0, 0};
        }
    }
    free(copy);
}
static void browse(const char *path) {
    char directory[4096];
    const char *root = getenv("LILSMOKE_OD_ROM_DIR");
    if (!strcmp(path, ".") && getcwd(directory, sizeof(directory))) path = directory;
    if (root && !strncmp(path, root, strlen(root))) fprintf(stderr, "OD_QA browse=%s\n", path);
}
DIR *opendir(const char *path) {
    DIR *(*real)(const char *) = dlsym(RTLD_NEXT, "opendir");
    browse(path); return real(path);
}
int scandir(const char *path, struct dirent ***entries, int (*filter)(const struct dirent *),
            int (*compare)(const struct dirent **, const struct dirent **)) {
    int (*real)(const char *, struct dirent ***, int (*)(const struct dirent *),
                int (*)(const struct dirent **, const struct dirent **)) = dlsym(RTLD_NEXT, "scandir");
    browse(path); return real(path, entries, filter, compare);
}
int SDL_Init(Uint32 flags) {
    int (*real)(Uint32) = dlsym(RTLD_NEXT, "SDL_Init");
    int result = real(flags);
    fprintf(stderr, "OD_QA SDL_Init flags=%u result=%d error=%s\n", flags, result, SDL_GetError());
    return result;
}
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}
static void capture(SDL_Surface *surface) {
    char path[4096];
    double elapsed;
    if (!surface) return;
    observed = surface;
    if (!started) started = now();
    elapsed = now() - started;
    if ((!captured && elapsed > setting("OD_QA_BEFORE_AT", 2.5)) ||
        (captured == 1 && elapsed > setting("OD_QA_AFTER_AT", 4.5))) {
        snprintf(path, sizeof(path), "%s/%s.bmp", getenv("OD_QA_OUTPUT"), captured ? "after" : "before");
        SDL_SaveBMP(surface, path);
        fprintf(stderr, "OD_QA frame=%s %dx%d pitch=%d bpp=%d\n",
                captured ? "after" : "before", surface->w, surface->h, surface->pitch, surface->format->BitsPerPixel);
        ++captured;
    }
    if (elapsed > setting("OD_QA_QUIT_AT", 5.5) + 3) _exit(90);
}
SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags) {
    SDL_Surface *(*real)(int, int, int, Uint32) = dlsym(RTLD_NEXT, "SDL_SetVideoMode");
    SDL_Surface *surface = real(w, h, bpp, flags);
    if (surface) { observed = surface; if (!started) started = now(); }
    return surface;
}
int SDL_Flip(SDL_Surface *surface) {
    int (*real)(SDL_Surface *) = dlsym(RTLD_NEXT, "SDL_Flip");
    capture(surface); return real(surface);
}
void SDL_UpdateRect(SDL_Surface *surface, Sint32 x, Sint32 y, Uint32 w, Uint32 h) {
    void (*real)(SDL_Surface *, Sint32, Sint32, Uint32, Uint32) = dlsym(RTLD_NEXT, "SDL_UpdateRect");
    capture(surface); real(surface, x, y, w, h);
}
void SDL_UpdateRects(SDL_Surface *surface, int count, SDL_Rect *rects) {
    void (*real)(SDL_Surface *, int, SDL_Rect *) = dlsym(RTLD_NEXT, "SDL_UpdateRects");
    capture(surface); real(surface, count, rects);
}
static void pump(void) {
    double elapsed;
    void (*real)(void) = dlsym(RTLD_NEXT, "SDL_PumpEvents");
    real();
    if (!started) return;
    elapsed = now() - started;
    // Some native menus retain the returned state pointer between polls.
    // Keep that buffer current, as SDL itself does for real keyboard input.
    {
        Uint8 *(*get_keys)(int *) = dlsym(RTLD_NEXT, "SDL_GetKeyState");
        int count;
        Uint8 *keys = get_keys(&count);
        setup_actions();
        for (int i = 0; i < action_count; ++i)
            if (actions[i].key >= 0 && actions[i].key < count)
                keys[actions[i].key] = elapsed > actions[i].at && elapsed < actions[i].until;
    }
    if ((!captured && elapsed > setting("OD_QA_BEFORE_AT", 2.5)) ||
        (captured == 1 && elapsed > setting("OD_QA_AFTER_AT", 4.5))) capture(observed);
    if (elapsed > setting("OD_QA_QUIT_AT", 5.5) + 3) _exit(90);
}
Uint8 *SDL_GetKeyState(int *number) {
    Uint8 *(*real)(int *) = dlsym(RTLD_NEXT, "SDL_GetKeyState");
    Uint8 *keys;
    int count, i;
    double elapsed;
    pump(); keys = real(number);
    setup_actions();
    elapsed = started ? now() - started : 0;
    real(&count);
    for (i = 0; i < action_count; ++i)
        if (actions[i].key >= 0 && actions[i].key < count)
            keys[actions[i].key] = 0;
    for (i = 0; i < action_count; ++i)
        if (actions[i].key >= 0 && actions[i].key < count &&
            elapsed > actions[i].at && elapsed < actions[i].until) keys[actions[i].key] = 1;
    return keys;
}
void SDL_PumpEvents(void) { pump(); }
static int synthetic(SDL_Event *event, Uint32 mask) {
    double elapsed;
    int i;
    if (!started || !event) return 0;
    elapsed = now() - started;
    setup_actions();
    for (i = 0; i < action_count; ++i) {
        struct action *action = &actions[i];
        if ((!action->down && elapsed > action->at) || (!action->up && elapsed > action->until)) {
            int type = action->down ? SDL_KEYUP : SDL_KEYDOWN;
            if (!(mask & SDL_EVENTMASK(type))) continue;
            memset(event, 0, sizeof(*event));
            event->type = action->down ? SDL_KEYUP : SDL_KEYDOWN;
            event->key.state = action->down ? SDL_RELEASED : SDL_PRESSED;
            event->key.keysym.sym = action->key;
            fprintf(stderr, "OD_QA key=%d type=%d\n", action->key, event->type);
            if (action->down) action->up = 1; else action->down = 1;
            return 1;
        }
    }
    if (!sent_quit && (mask & SDL_QUITMASK) && elapsed > setting("OD_QA_QUIT_AT", 5.5)) {
        memset(event, 0, sizeof(*event)); event->type = SDL_QUIT; sent_quit = 1; return 1;
    }
    return 0;
}
int SDL_PollEvent(SDL_Event *event) {
    int (*real)(SDL_Event *) = dlsym(RTLD_NEXT, "SDL_PollEvent");
    pump();
    return synthetic(event, SDL_ALLEVENTS) ? 1 : real(event);
}
int SDL_PeepEvents(SDL_Event *events, int count, SDL_eventaction action, Uint32 mask) {
    int (*real)(SDL_Event *, int, SDL_eventaction, Uint32) = dlsym(RTLD_NEXT, "SDL_PeepEvents");
    pump();
    if (action == SDL_GETEVENT && count > 0 && events && synthetic(events, mask)) return 1;
    return real(events, count, action, mask);
}
