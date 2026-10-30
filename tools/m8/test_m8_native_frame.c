#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
static int games, menus, resets, captures, fail;
static int game(const void *p, unsigned w, unsigned h, unsigned pitch, int rgb32, unsigned n, unsigned d) {
    (void)p; assert(w == 240 && h == 160 && pitch == 480 && !rgb32 && n == 15000 && d == 10000);
    ++games; return !fail;
}
static int menu(const void *p, unsigned w, unsigned h, unsigned pitch, int rgb32) {
    (void)p; assert(w == 426 && h == 240 && pitch == 852 && !rgb32);
    ++menus; return !fail;
}
static void reset(void) { ++resets; }
static void capture(void) { ++captures; }
static void *lookup(void *handle, const char *name) {
    assert(handle == RTLD_DEFAULT);
    if (!strcmp(name, "lilsmoke_present_rgb")) return (void *)game;
    if (!strcmp(name, "lilsmoke_present_menu")) return (void *)menu;
    if (!strcmp(name, "lilsmoke_present_reset")) return (void *)reset;
    if (!strcmp(name, "lilsmoke_present_checkpoint")) return (void *)capture;
    abort();
}
#define dlsym lookup
#include "../../package/retroarch-mips/m8_native_frame.h"
int main(void) {
    uint16_t pixels[426 * 240] = {0};
    unsetenv("LILSMOKE_HCFB");
    assert(!m8_native_frame(pixels, 240, 160, 480, 0, 1.5f, 0, 0));
    assert(!m8_native_menu_frame(pixels, 426, 240, 852, 0));
    assert(!games && !menus);
    setenv("LILSMOKE_HCFB", "1", 1);
    for (int i = 0; i < 8; ++i) {
        assert(m8_native_frame(pixels, 240, 160, 480, 0, 1.5f, 0, 0));
        assert(m8_native_menu_frame(pixels, 426, 240, 852, 0));
        assert(m8_native_frame(pixels, 240, 160, 480, 0, 1.5f, 0, 0));
    }
    assert(games == 16 && menus == 8 && captures == 8 && !resets);
    assert(!m8_native_frame(pixels, 240, 160, 480, 0, 1.5f, 1, 0));
    assert(resets == 1 && !m8_presentation.active);
    fail = 1;
    assert(!m8_native_menu_frame(pixels, 426, 240, 852, 0));
    assert(captures == 8);
    m8_native_cache_release();
    assert(!m8_presentation.game && !m8_presentation.cached);
    puts("PASS: native game/menu/game transitions, no borrowed-frame retention, disabled gating, checkpoint and software fallback.");
    return 0;
}
