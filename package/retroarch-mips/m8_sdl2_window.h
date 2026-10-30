#ifndef M8_SDL2_WINDOW_H
#define M8_SDL2_WINDOW_H
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
/* A windowed client can be larger than DirectFB's physical layer. Select
 * exclusive fullscreen, as LilSmokeUI does, instead of the stock desktop. */
static bool m8_sdl2_window(unsigned *width, unsigned *height, unsigned *flags) {
    const char *enabled = getenv("LILSMOKE_HCFB");
    if (!enabled || strcmp(enabled, "1")) return false;
    *width = 1280;
    *height = 720;
    const char *w = getenv("LILSMOKE_SCREEN_WIDTH");
    const char *h = getenv("LILSMOKE_SCREEN_HEIGHT");
    if (w && h) {
        char *endw, *endh;
        unsigned long requested_w = strtoul(w, &endw, 10);
        unsigned long requested_h = strtoul(h, &endh, 10);
        if (*w && *h && !*endw && !*endh && requested_w >= 320 && requested_w <= 1920 &&
            requested_h >= 240 && requested_h <= 1080) { *width = requested_w; *height = requested_h; }
    }
    *flags = 0x00000001U; /* SDL_WINDOW_FULLSCREEN, never FULLSCREEN_DESKTOP. */
    return true;
}
#endif
