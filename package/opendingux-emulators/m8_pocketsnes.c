#include "m8_pocketsnes.h"
#include <SDL.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

static struct m8_pacer pacer;
static struct m8_chord chords[2];
static uint32_t repeat_last[2];
static uint64_t repeat_at[2];
static uint64_t stats_start, render_us;
static unsigned emulated, rendered;
static char fps[24] = "E0.0 R0.0";
static int native_video;

uint64_t m8_now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

void m8_timing_reset(unsigned period)
{
    stats_start = m8_now_us();
    emulated = rendered = 0;
    render_us = 0;
    m8_pacer_reset(&pacer, stats_start, period);
    fprintf(stderr, "POCKETSNES_PACING clock=monotonic guest_us=%u max_skip=%d audio_clock=0\n",
            pacer.period, M8_MAX_SKIP);
}

int m8_sync(int manual_skip)
{
    uint64_t wait;
    int render = m8_pacer_step(&pacer, m8_now_us(), manual_skip, &wait);
    if (wait) {
        struct timespec remaining = { wait / 1000000, (wait % 1000000) * 1000 };
        while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {}
    }
    return render;
}

void m8_frame_completed(void)
{
    uint64_t now = m8_now_us(), elapsed = now - stats_start;
    ++emulated;
    if (elapsed >= 5000000) {
        double seconds = elapsed / 1000000.0;
        snprintf(fps, sizeof(fps), "E%.1f R%.1f", emulated / seconds, rendered / seconds);
        fprintf(stderr, "POCKETSNES_PERF elapsed_us=%llu emulated=%u rendered=%u emu_fps=%.2f render_fps=%.2f present_ms=%.2f\n",
                (unsigned long long)elapsed, emulated, rendered, emulated / seconds,
                rendered / seconds, rendered ? render_us / (1000.0 * rendered) : 0.0);
        stats_start = now;
        emulated = rendered = 0;
        render_us = 0;
    }
}

void m8_frame_rendered(uint64_t start)
{
    ++rendered;
    render_us += m8_now_us() - start;
}

const char *m8_fps_text(void) { return fps; }
int m8_video_native(void) { return native_video; }
void m8_video_set_native(int value) { native_video = value; }

int m8_video_aspect(void)
{
    typedef void *(*get_window_fn)(void);
    typedef void *(*get_renderer_fn)(void *);
    typedef int (*logical_fn)(void *, int, int);
    typedef int (*output_fn)(void *, int *, int *);
    static void *sdl2;
    get_window_fn window = (get_window_fn)SDL_GL_GetProcAddress("SDL12COMPAT_GetWindow");
    get_renderer_fn renderer;
    logical_fn logical;
    output_fn output;
    void *r;
    int w = 0, h = 0;
    if (!window) return 0;
    if (!sdl2) sdl2 = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!sdl2) return 0;
    renderer = (get_renderer_fn)dlsym(sdl2, "SDL_GetRenderer");
    logical = (logical_fn)dlsym(sdl2, "SDL_RenderSetLogicalSize");
    output = (output_fn)dlsym(sdl2, "SDL_GetRendererOutputSize");
    if (!renderer || !logical || !output || !(r = renderer(window()))) return 0;
    /* SDL12-compat presents its texture with a NULL destination, filling this
       4:3 viewport. Only the final software renderer scales native SNES pixels. */
    if (logical(r, 320, 240) != 0 || output(r, &w, &h) != 0) return 0;
    fprintf(stderr, "POCKETSNES_VIDEO native=%dx%d bpp=%u pitch=%u output=%dx%d aspect=4:3 scale_passes=1\n",
            SDL_GetVideoSurface()->w, SDL_GetVideoSurface()->h,
            SDL_GetVideoSurface()->format->BitsPerPixel, SDL_GetVideoSurface()->pitch, w, h);
    return 1;
}

void m8_input_reset(unsigned player)
{
    if (player >= 2) return;
    memset(&chords[player], 0, sizeof(chords[player]));
    repeat_last[player] = 0;
    repeat_at[player] = 0;
}

uint32_t m8_input_filter(unsigned player, uint32_t raw, int repeat)
{
    uint64_t now = m8_now_us();
    uint32_t value, edge;
    if (player >= 2) return 0;
    value = m8_chord_filter(&chords[player], raw, now);
    if (!repeat) {
        repeat_last[player] = 0;
        return value;
    }
    edge = value & ~repeat_last[player];
    if (edge || !value) repeat_at[player] = now + 300000;
    else if (now >= repeat_at[player]) {
        edge = value;
        repeat_at[player] = now + 80000;
    }
    repeat_last[player] = value;
    return edge;
}
