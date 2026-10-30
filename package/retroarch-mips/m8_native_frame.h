#ifndef M8_NATIVE_FRAME_H
#define M8_NATIVE_FRAME_H
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef int (*m8_present_fn)(const void *, unsigned, unsigned, unsigned, int, unsigned, unsigned);
typedef int (*m8_menu_fn)(const void *, unsigned, unsigned, unsigned, int);
typedef void (*m8_reset_fn)(void);
static struct {
    m8_present_fn present;
    m8_menu_fn menu;
    m8_reset_fn reset, checkpoint;
    int looked_up, active, in_menu, cached;
    void *game;
    size_t capacity;
    unsigned width, height, pitch;
    int rgb32;
    float aspect;
} m8_presentation;

static int m8_native_enabled(void) {
    const char *setting = getenv("LILSMOKE_HCFB");
    return setting && setting[0] == '1' && !setting[1];
}
static void m8_lookup_presentation(void) {
    if (m8_presentation.looked_up) return;
    m8_presentation.present = (m8_present_fn)dlsym(RTLD_DEFAULT, "lilsmoke_present_rgb");
    m8_presentation.menu = (m8_menu_fn)dlsym(RTLD_DEFAULT, "lilsmoke_present_menu");
    m8_presentation.reset = (m8_reset_fn)dlsym(RTLD_DEFAULT, "lilsmoke_present_reset");
    m8_presentation.checkpoint = (m8_reset_fn)dlsym(RTLD_DEFAULT, "lilsmoke_present_checkpoint");
    m8_presentation.looked_up = 1;
}
static void m8_native_cache_release(void) {
    if (m8_presentation.active && m8_presentation.reset) m8_presentation.reset();
    free(m8_presentation.game);
    m8_presentation.game = NULL;
    m8_presentation.capacity = 0;
    m8_presentation.cached = m8_presentation.active = m8_presentation.in_menu = 0;
}
static void m8_cache_game(const void *frame, unsigned w, unsigned h,
                          unsigned pitch, int rgb32, float aspect) {
    const unsigned row = w * (rgb32 ? 4U : 2U);
    const size_t bytes = (size_t)row * h;
    if (bytes > m8_presentation.capacity) {
        void *next = realloc(m8_presentation.game, bytes);
        if (!next) { m8_presentation.cached = 0; return; }
        m8_presentation.game = next;
        m8_presentation.capacity = bytes;
    }
    for (unsigned y = 0; y < h; ++y)
        memcpy((unsigned char *)m8_presentation.game + (size_t)y * row,
               (const unsigned char *)frame + (size_t)y * pitch, row);
    m8_presentation.width = w; m8_presentation.height = h; m8_presentation.pitch = row;
    m8_presentation.rgb32 = rgb32; m8_presentation.aspect = aspect;
    m8_presentation.cached = 1;
}
static int m8_native_menu_frame(const void *frame, unsigned w, unsigned h,
                                 unsigned pitch, int rgb32) {
    if (!m8_native_enabled()) return 0;
    m8_lookup_presentation();
    if (!m8_presentation.menu) return 0;
    m8_presentation.active = m8_presentation.menu(frame, w, h, pitch, rgb32);
    if (m8_presentation.active) m8_presentation.in_menu = 1;
    if (m8_presentation.active && m8_presentation.checkpoint) m8_presentation.checkpoint();
    return m8_presentation.active;
}
static int m8_native_frame(const void *frame, unsigned w, unsigned h,
                          unsigned pitch, int rgb32, float aspect, int menu, unsigned rotation) {
    if (!m8_native_enabled()) return 0;
    m8_lookup_presentation();
    if (menu || rotation) {
        if (m8_presentation.active && m8_presentation.reset) m8_presentation.reset();
        m8_presentation.active = 0;
        m8_presentation.in_menu = 0;
        return 0;
    }
    if (!m8_presentation.present) return 0;
    if (!frame) {
        /* Libretro NULL means repeat, not blank. Never leave native mode for
         * a duplicate; restore our owned snapshot after an RGUI mode switch. */
        if (m8_presentation.active && !m8_presentation.in_menu) return 1;
        if (!m8_presentation.cached) return 0;
        m8_presentation.active = m8_presentation.present(m8_presentation.game,
            m8_presentation.width, m8_presentation.height, m8_presentation.pitch,
            m8_presentation.rgb32, (unsigned)(m8_presentation.aspect * 10000.0f + 0.5f), 10000);
        if (m8_presentation.active) m8_presentation.in_menu = 0;
        return m8_presentation.active;
    }
    if (!w || !h || w > 640 || h > 480 || pitch < w * (rgb32 ? 4U : 2U) ||
        pitch > 16384 || !(aspect >= 0.5f && aspect <= 3.0f)) return 0;
    m8_presentation.active = m8_presentation.present(frame, w, h, pitch, rgb32,
                                                  (unsigned)(aspect * 10000.0f + 0.5f), 10000);
    if (m8_presentation.active) {
        m8_presentation.in_menu = 0;
        m8_cache_game(frame, w, h, pitch, rgb32, aspect);
    }
    return m8_presentation.active;
}
#endif
