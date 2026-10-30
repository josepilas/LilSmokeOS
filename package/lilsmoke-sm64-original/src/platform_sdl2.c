/* SPDX-License-Identifier: MPL-2.0
 * Platform-only adapter. No replacement SM64 game logic or renderer. */
#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include "sm64.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/mario.h"
#include "game/level_update.h"
#include "pc/audio/audio_api.h"
#include "pc/controller/controller_api.h"
#include "pc/gfx/gfx_window_manager_api.h"
#include "pc/gfx/gfx_soft.h"
#include "pc/gfx/gfx_pc.h"
#include "input_bridge.h"
#include "m8_pocketsnes_audio.h"
#include "audio_resample.h"
#include "frame_pacing.h"
#include "world_quality.h"
#include "world_scale.h"
#include "menu.h"

extern void game_exit(void);
extern uint32_t texcache_size;
int stop_menu_loop;
static SDL_Window *window;
static SDL_Surface *pixels;
static SDL_Surface *world_pixels;
static SDL_Surface *world_surfaces[4];
static int overlay, world_filter, world_smooth, test_width_cycle, test_world_evidence;
static struct sm64_world_scaler world_scaler;
static struct sm64_world_quality quality;
#ifdef CONVERT
typedef uint16_t frame_pixel;
#define PIXEL_BITS 16
#define RED_MASK 0xf800
#define GREEN_MASK 0x07e0
#define BLUE_MASK 0x001f
#define ALPHA_MASK 0
#else
typedef uint32_t frame_pixel;
#define PIXEL_BITS 32
#define RED_MASK 0xff0000
#define GREEN_MASK 0xff00
#define BLUE_MASK 0xff
#define ALPHA_MASK 0xff000000
#endif
static frame_pixel *world_storage;
static SDL_AudioDeviceID audio_device;
static int native_audio;
#define PCM_PENDING_FRAMES (M8_PCM_CHUNK_FRAMES * 4)
static int16_t pcm_pending[PCM_PENDING_FRAMES * 2];
static unsigned pcm_head, pcm_count;
static uint64_t pcm_overflow;
static struct sm64_resampler resampler;
typedef int (*native_present_fn)(const void *, unsigned, unsigned, unsigned, int, unsigned, unsigned);
static native_present_fn native_present;
static void (*native_reset)(void);
static int native_video_active;
static unsigned interactive_after = 5000;
static unsigned frame, limit, stride = 1;
static int scripted, preflight;
static unsigned rendered, audio_blocks, audio_nonzero;
static uint32_t audio_hash = 2166136261u;
static uint64_t started;
static struct sm64_frame_pacer pacer;
static unsigned render_width = 320, render_height = 240;
static uint64_t work_ticks, raster_ticks, synthesis_ticks, output_ticks, scale_ticks, profile_started;
static unsigned profile_frames;
static struct sm64_menu menu;
static int menu_active;
static void show_menu(void);
static void pcm_flush(void);
static void snapshot(const char *kind);

extern void __real_gfx_run(Gfx *commands);
void __wrap_gfx_run(Gfx *commands) {
    uint64_t before = SDL_GetPerformanceCounter();
    uint64_t output_before = output_ticks;
    __real_gfx_run(commands);
    uint64_t elapsed = SDL_GetPerformanceCounter() - before;
    uint64_t presentation = output_ticks - output_before;
    raster_ticks += elapsed > presentation ? elapsed - presentation : 0;
}
extern void __real_create_next_audio_buffer(s16 *samples, u32 count);
void __wrap_create_next_audio_buffer(s16 *samples, u32 count) {
    uint64_t before = SDL_GetPerformanceCounter();
    __real_create_next_audio_buffer(samples, count);
    synthesis_ticks += SDL_GetPerformanceCounter() - before;
}

static unsigned env_number(const char *name, unsigned fallback, unsigned maximum) {
    const char *text = getenv(name);
    char *end;
    if (!text) return fallback;
    unsigned long value = strtoul(text, &end, 10);
    if (!*text || *end || value > maximum) { fprintf(stderr, "invalid %s\n", name); exit(2); }
    return (unsigned)value;
}
static void set_world_width(unsigned width) {
    render_width = width; render_height = width * 3 / 4;
    world_pixels = world_surfaces[sm64_width_index(width)];
    sm64_world_scaler_init(&world_scaler, width);
}
static void init(const char *name, bool fullscreen) {
    (void)fullscreen;
    limit = env_number("SM64_ORIGINAL_TEST_FRAMES", 0, 5000);
    stride = env_number("SM64_ORIGINAL_RENDER_EVERY", 1, 120);
    if (!stride) exit(2);
    scripted = limit != 0;
    preflight = env_number("SM64_ORIGINAL_PREFLIGHT", 0, 1);
    if (preflight && limit != 30) {
        fprintf(stderr, "preflight requires exactly 30 frames\n"); exit(2);
    }
    const char *hcfb = getenv("LILSMOKE_HCFB");
    const char *preset = getenv("SM64_WORLD_QUALITY");
    unsigned default_width = 256;
    int adaptive = 2;
    if (preset && *preset) {
        if (!strcmp(preset, "performance")) { default_width = 192; adaptive = 0; }
        else if (!strcmp(preset, "balanced")) adaptive = 0;
        else if (!strcmp(preset, "quality")) { default_width = 320; adaptive = 0; }
        else if (!strcmp(preset, "adaptive")) adaptive = 1;
        else if (!strcmp(preset, "detail")) adaptive = 2;
        else { fprintf(stderr, "invalid SM64_WORLD_QUALITY\n"); exit(2); }
    }
    render_width = env_number("SM64_SOFTWARE_WIDTH", default_width, 320);
    if (render_width != 160 && render_width != 192 && render_width != 256 && render_width != 320) exit(2);
    /* A legacy explicit width remains a fixed, authoritative override. */
    if (getenv("SM64_SOFTWARE_WIDTH")) adaptive = 0;
    quality = sm64_quality_init(render_width, adaptive);
    menu.quality = adaptive==2?0:adaptive?4:render_width==320?2:render_width==192?3:1;
    menu.volume = env_number("SM64_APP_VOLUME",100,100);
    render_height = render_width * 3 / 4;
    world_filter = env_number("SM64_WORLD_FILTER", 0, 1);
    const char *scale = getenv("SM64_WORLD_SCALE");
    if (scale && *scale && strcmp(scale, "nearest") && strcmp(scale, "smooth")) {
        fprintf(stderr, "invalid SM64_WORLD_SCALE\n"); exit(2);
    }
    world_smooth = scale && !strcmp(scale, "smooth");
    menu.smooth = world_smooth;
    test_width_cycle = scripted && env_number("SM64_ORIGINAL_TEST_WIDTH_CYCLE", 0, 1);
    test_world_evidence = scripted && env_number("SM64_ORIGINAL_TEST_WORLD_EVIDENCE", 0, 1);
    if (test_width_cycle) quality.adaptive = 0;
    interactive_after = env_number("SM64_ORIGINAL_INTERACTIVE_AFTER", 5000, 5000);
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER | (preflight ? 0 : SDL_INIT_VIDEO)) < 0) {
        fprintf(stderr, "SDL init: %s\n", SDL_GetError()); exit(2);
    }
    if (!preflight) window = SDL_CreateWindow(name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              320, 240, scripted ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    pixels = SDL_CreateRGBSurface(0, 320, 240, PIXEL_BITS, RED_MASK, GREEN_MASK, BLUE_MASK, ALPHA_MASK);
    unsigned capacity_width = 320;
    world_storage = calloc(capacity_width * capacity_width * 3 / 4, sizeof(*world_storage));
    const unsigned widths[] = {160, 192, 256, 320};
    for (unsigned i = 0; i < 4 && widths[i] <= capacity_width; ++i) {
        world_surfaces[i] = SDL_CreateRGBSurfaceFrom(world_storage, widths[i], widths[i] * 3 / 4,
            PIXEL_BITS, widths[i] * sizeof(frame_pixel), RED_MASK, GREEN_MASK, BLUE_MASK, ALPHA_MASK);
        if (!world_surfaces[i]) { fprintf(stderr, "SDL world surface: %s\n", SDL_GetError()); exit(2); }
    }
    set_world_width(render_width);
    if ((!preflight && !window) || !pixels || !world_storage || !world_pixels || pixels->pitch != 320 * (int)sizeof(frame_pixel) || world_pixels->pitch != (int)render_width * (int)sizeof(frame_pixel)) {
        fprintf(stderr, "SDL surface: %s\n", SDL_GetError()); exit(2);
    }
    gfx_output = world_pixels->pixels;
    SDL_ShowCursor(SDL_DISABLE);
    if (hcfb && !strcmp(hcfb, "1")) {
        native_present = (native_present_fn)dlsym(RTLD_DEFAULT, "lilsmoke_present_rgb");
        native_reset = (void (*)(void))dlsym(RTLD_DEFAULT, "lilsmoke_present_reset");
    }
    started = SDL_GetPerformanceCounter();
    pacer = (struct sm64_frame_pacer){started, 0};
    profile_started = started;
    fprintf(stderr, "SM64_VIDEO world=%ux%u hud=320x240 filter=%d aspect=4:3 every_frame=%d quality=%s scale=%s world_bytes=%u\n",
        render_width, render_height, world_filter, stride == 1,
        adaptive == 2 ? "detail" : adaptive ? "adaptive" : "fixed", world_smooth ? "smooth" : "nearest",
        capacity_width * capacity_width * 3 / 4 * (unsigned)sizeof(frame_pixel));
}
static void callbacks(bool (*down)(int), bool (*up)(int), void (*release)(void)) {
    (void)down; (void)up; (void)release;
}
static void fullscreen_callback(void (*callback)(bool)) { (void)callback; }
static void fullscreen(bool enabled) { (void)enabled; }
static void dimensions(uint32_t *width, uint32_t *height) {
    *width = overlay ? 320 : render_width; *height = overlay ? 240 : render_height;
}
bool sm64_raster_filter_enabled(void) { return overlay || world_filter; }
/* Switch only once after the world, before the existing HUD/dialog display list. */
void gfx_sdl_upscale_to_fullscreen(void) {
    if (overlay) return;
    uint64_t before = SDL_GetPerformanceCounter();
    sm64_world_scale(&world_scaler, world_pixels->pixels, pixels->pixels, world_smooth);
    scale_ticks += SDL_GetPerformanceCounter() - before;
    if (test_world_evidence) snapshot("world");
    overlay = 1; gfx_output = pixels->pixels;
    gfx_current_dimensions.width = 320; gfx_current_dimensions.height = 240;
    gfx_soft_api.on_resize();
}
static void events(void) {
    unsigned width = quality.width;
    if (test_width_cycle) {
        const unsigned widths[] = {160, 192, 256, 320, 256, 192};
        width = widths[(frame / 300) % 6];
    }
    int resized = width != render_width;
    if (resized) {
        fprintf(stderr, "SM64_QUALITY frame=%u world=%ux%u previous=%u\n", frame, width, width * 3 / 4, render_width);
        set_world_width(width);
    }
    if (overlay || resized) {
        overlay = 0; gfx_output = world_pixels->pixels;
        gfx_current_dimensions.width = render_width; gfx_current_dimensions.height = render_height;
        gfx_soft_api.on_resize();
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT ||
        (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) game_exit();
    if (sm64_native_input_poll()) game_exit();
}
static bool start_frame(void) { return !scripted || frame % stride == 0 || frame + 60 >= limit; }
static void snapshot(const char *kind) {
    const char *root = getenv("SM64_ORIGINAL_EVIDENCE");
    char path[1024];
    if (!root || (frame != 45 && frame != 240 && frame != 390 && frame != 510 &&
                  frame != 1200 && frame != 1440 && frame != 2100 && frame != 2300 &&
                  frame != 2700 && frame + 1 != limit)) return;
    if (snprintf(path, sizeof(path), "%s/%s-%04u.ppm", root, kind, frame) >= (int)sizeof(path)) exit(2);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) { perror("private evidence"); exit(2); }
    FILE *file = fdopen(fd, "wb");
    if (!file) exit(2);
    fputs("P6\n320 240\n255\n", file);
    for (unsigned y = 0; y < 240; ++y) for (unsigned x = 0; x < 320; ++x) {
        const uint8_t *p = (const uint8_t *)pixels->pixels + y * pixels->pitch + x * sizeof(frame_pixel);
#ifdef CONVERT
        const uint16_t color = *(const uint16_t *)p;
        const uint8_t r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
        const uint8_t rgb[3] = {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
#else
        const uint8_t rgb[3] = {p[2], p[1], p[0]};
#endif
        fwrite(rgb, 1, 3, file);
    }
    if (fclose(file)) exit(2);
}
static void swap_begin(void) {
    uint64_t before = SDL_GetPerformanceCounter();
    gfx_sdl_upscale_to_fullscreen();
    ++rendered;
    snapshot("frame");
    if (preflight) {
        output_ticks += SDL_GetPerformanceCounter() - before;
        return;
    }
    if (native_present) {
        native_video_active = native_present(pixels->pixels, 320, 240, pixels->pitch, PIXEL_BITS==32, 4, 3);
        if (native_video_active) {
            output_ticks += SDL_GetPerformanceCounter() - before;
            return;
        }
    }
    SDL_Surface *screen = SDL_GetWindowSurface(window);
    if (!screen || SDL_BlitScaled(pixels, NULL, screen, NULL) < 0 || SDL_UpdateWindowSurface(window) < 0) {
        fprintf(stderr, "SDL presentation: %s\n", SDL_GetError()); exit(2);
    }
    output_ticks += SDL_GetPerformanceCounter() - before;
}
static void noop(void) {}
static double get_time(void) { return (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency(); }
static void shutdown(void) {
    sm64_native_input_close();
    if (native_video_active && native_reset) native_reset();
    native_video_active = 0;
    SDL_FreeSurface(pixels); pixels = NULL; gfx_output = NULL;
    for (unsigned i = 0; i < 4; ++i) { SDL_FreeSurface(world_surfaces[i]); world_surfaces[i] = NULL; }
    world_pixels = NULL; free(world_storage); world_storage = NULL;
    SDL_DestroyWindow(window); window = NULL; SDL_Quit();
}
static void main_loop(void (*iteration)(void)) {
    int requested=(!scripted || frame>=interactive_after) && sm64_native_input_take_menu();
    if (menu_active || requested) {
        int was_open=menu_active;
        if (!menu_active) {
            menu_active=1;menu.previous=UINT32_MAX;
            if (native_audio) { m8_pcm_close();pcm_head=pcm_count=0; }
            if (audio_device) SDL_PauseAudioDevice(audio_device,1);
            fprintf(stderr,"SM64_MENU opened frame=%u timer=%u game_paused=1\n",frame,gGlobalTimer);
        }
        if (sm64_native_input_poll()) game_exit();
        int pending=sm64_native_input_take_menu();
        int action=was_open && (requested||pending)?1:sm64_menu_step(&menu,sm64_native_input_menu_buttons(),getenv("HOME"));
        if (action==2) game_exit();
        if (action==3) {
            unsigned width=menu.quality==2?320:menu.quality==3?192:256;
            quality=sm64_quality_init(width,menu.quality==0?2:menu.quality==4?1:0);
            world_smooth=menu.smooth;
        }
        if (action==1) {
            menu_active=0;
            sm64_native_input_suppress_buttons();
            if (native_audio) m8_pcm_init(M8_PCM_RATE,2,16);
            if (audio_device) { SDL_ClearQueuedAudio(audio_device);SDL_PauseAudioDevice(audio_device,0); }
            pacer=(struct sm64_frame_pacer){SDL_GetPerformanceCounter(),0};
            profile_started=SDL_GetPerformanceCounter();profile_frames=0;
            work_ticks=raster_ticks=synthesis_ticks=output_ticks=scale_ticks=0;
            fprintf(stderr,"SM64_MENU resumed frame=%u timer=%u\n",frame,gGlobalTimer);
        } else { show_menu();SDL_Delay(16);return; }
    }
    uint64_t before = SDL_GetPerformanceCounter();
    uint64_t raster_before = raster_ticks;
    iteration();
    uint64_t now = SDL_GetPerformanceCounter(), frequency = SDL_GetPerformanceFrequency();
    work_ticks += now - before;
    sm64_quality_sample(&quality, (now - before) * 1000000 / frequency,
        (raster_ticks - raster_before) * 1000000 / frequency);
    ++profile_frames;
    if (now - profile_started >= frequency * 5 || (scripted && frame + 1 == limit)) {
        double per_frame = 1000.0 / (frequency * (double)profile_frames);
        fprintf(stderr, "SM64_PROFILE frames=%u fps=%.2f work_ms=%.2f raster_ms=%.2f synth_ms=%.2f output_ms=%.2f scale_ms=%.2f world=%ux%u\n",
                profile_frames, profile_frames * (double)frequency / (now - profile_started),
                work_ticks * per_frame, raster_ticks * per_frame, synthesis_ticks * per_frame, output_ticks * per_frame,
                scale_ticks * per_frame, render_width, render_height);
        work_ticks = raster_ticks = synthesis_ticks = output_ticks = scale_ticks = 0;
        profile_frames = 0; profile_started = now;
    }
    if (scripted && frame % 30 == 0) {
        printf("OriginalFrame=%u Level=%d Area=%d Timer=%u TextureCache=%u Action=%08x Position=%.1f,%.1f,%.1f\n", frame,
               gCurrLevelNum, gCurrentArea ? gCurrentArea->index : -1, gGlobalTimer, texcache_size,
               gMarioState ? gMarioState->action : 0, gMarioState ? gMarioState->pos[0] : 0,
               gMarioState ? gMarioState->pos[1] : 0, gMarioState ? gMarioState->pos[2] : 0);
        fflush(stdout);
    }
    ++frame;
    if (scripted && frame >= limit) {
        double seconds = (double)(SDL_GetPerformanceCounter() - started) / SDL_GetPerformanceFrequency();
        printf("OriginalEngineTest=FINISHED Frames=%u Rendered=%u Seconds=%.3f AudioBlocks=%u AudioNonzero=%u AudioHash=%08x\n",
               frame, rendered, seconds, audio_blocks, audio_nonzero, audio_hash);
        fflush(stdout); game_exit();
    }
    if (!scripted) {
        uint64_t frequency = SDL_GetPerformanceFrequency();
        uint64_t now = SDL_GetPerformanceCounter();
        uint64_t deadline = sm64_frame_deadline(&pacer, now, frequency);
        if (deadline > now) {
            while (deadline > now) {
                if (native_audio) pcm_flush();
                uint32_t remaining = (uint32_t)((deadline - now) * 1000 / frequency);
                SDL_Delay(native_audio && remaining > 4 ? 4 : remaining ? remaining : 1);
                now = SDL_GetPerformanceCounter();
            }
        }
    }
}
static void show_menu(void) {
    sm64_menu_render(&menu,pixels->pixels);
    if (native_present && native_present(pixels->pixels,320,240,pixels->pitch,PIXEL_BITS==32,4,3)) return;
    SDL_Surface *screen=SDL_GetWindowSurface(window);
    if (!screen || SDL_BlitScaled(pixels,NULL,screen,NULL)<0 || SDL_UpdateWindowSurface(window)<0) game_exit();
}
struct GfxWindowManagerAPI gfx_sdl = {
    init, callbacks, fullscreen_callback, fullscreen, main_loop, dimensions, events,
    start_frame, swap_begin, noop, get_time, shutdown
};

static void controller_init(void) { sm64_native_input_init(); }
static void controller_read(OSContPad *pad) {
    if (scripted && frame < interactive_after) {
        if (frame == 270) pad->button |= START_BUTTON;
        if (frame == 420 || (frame >= 480 && frame < 1200 && frame % 80 == 0)) pad->button |= A_BUTTON;
        if (frame >= 1440 && frame < 1500) pad->stick_y = 70;
        if (frame == 1470) pad->button |= A_BUTTON;
        if (frame >= 2200 && frame < 2600) pad->stick_y = 70;
        if (frame == 2300) pad->button |= A_BUTTON;
        if (frame == 2450) pad->button |= B_BUTTON;
        return;
    }
    // One canonical input owner avoids overlapping SDL/keymap keyboard bindings.
    sm64_native_input_read(&pad->button, &pad->stick_x, &pad->stick_y);
    if (scripted && (pad->button || pad->stick_x || pad->stick_y)) {
        printf("OriginalNativeInput Frame=%u Buttons=%04x Stick=%d,%d\n", frame, pad->button, pad->stick_x, pad->stick_y);
        fflush(stdout);
    }
}
struct ControllerAPI controller_sdl = {controller_init, controller_read};

static bool audio_init_adapter(void) {
    if (m8_pcm_requested()) {
        native_audio = 1;
        memset(&resampler, 0, sizeof(resampler));
        pcm_head = pcm_count = 0; pcm_overflow = 0;
        (void)m8_pcm_init(M8_PCM_RATE, 2, 16);
        fprintf(stderr, "SM64_AUDIO native=1 input_rate=32000 output_rate=44100 nonblocking=1\n");
        fprintf(stderr, "SM64_AUDIO resampler=integer-linear pending_ms=67\n");
        return true;
    }
    if (scripted) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return false;
    SDL_AudioSpec wanted = {0};
    wanted.freq = 32000; wanted.format = AUDIO_S16SYS; wanted.channels = 2; wanted.samples = 1024;
    audio_device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
    if (!audio_device) return false;
    SDL_PauseAudioDevice(audio_device, 0); return true;
}
static int audio_buffered(void) {
    if (native_audio) {
        struct m8_pcm_stats stats;
        m8_pcm_get_stats(&stats);
        return (int)((stats.inflight * M8_PCM_CHUNK_FRAMES + pcm_count) * 32000 / M8_PCM_RATE);
    }
    return audio_device ? SDL_GetQueuedAudioSize(audio_device) / 4 : 0;
}
static int audio_desired(void) { return native_audio ? 1067 : 1600; }
static void pcm_flush(void) {
    struct m8_pcm_stats stats;
    int16_t packet[M8_PCM_CHUNK_FRAMES * 2];
    for (unsigned budget = 0; budget < 2 && pcm_count >= M8_PCM_CHUNK_FRAMES; ++budget) {
        m8_pcm_get_stats(&stats);
        if (!stats.ready || stats.inflight >= 2) break;
        for (unsigned i = 0; i < M8_PCM_CHUNK_FRAMES; ++i) {
            unsigned source = (pcm_head + i) % PCM_PENDING_FRAMES;
            packet[i * 2] = pcm_pending[source * 2]; packet[i * 2 + 1] = pcm_pending[source * 2 + 1];
            if (menu.volume<100) {
                packet[i*2]=(int16_t)((int)packet[i*2]*(int)menu.volume/100);
                packet[i*2+1]=(int16_t)((int)packet[i*2+1]*(int)menu.volume/100);
            }
        }
        if (m8_pcm_send(packet, M8_PCM_CHUNK_FRAMES) != 1) break;
        pcm_head = (pcm_head + M8_PCM_CHUNK_FRAMES) % PCM_PENDING_FRAMES;
        pcm_count -= M8_PCM_CHUNK_FRAMES;
    }
}
static void pcm_sample(void *context, int16_t left, int16_t right) {
    (void)context;
    if (pcm_count == PCM_PENDING_FRAMES) {
        pcm_head = (pcm_head + 1) % PCM_PENDING_FRAMES;
        --pcm_count; ++pcm_overflow;
    }
    unsigned destination = (pcm_head + pcm_count++) % PCM_PENDING_FRAMES;
    pcm_pending[destination * 2] = left; pcm_pending[destination * 2 + 1] = right;
}
static void audio_play(const uint8_t *bytes, size_t size) {
    ++audio_blocks;
    if (scripted) for (size_t i = 0; i < size; ++i) {
        audio_nonzero += bytes[i] != 0; audio_hash = (audio_hash ^ bytes[i]) * 16777619u;
    }
    if (native_audio) {
        if (size > 16384 || size % 4) return;
        pcm_flush();
        sm64_resample(&resampler, (const int16_t *)bytes, size / 4, pcm_sample, NULL);
        pcm_flush();
        return;
    }
    if (audio_device && SDL_GetQueuedAudioSize(audio_device) < 32768 && SDL_QueueAudio(audio_device, bytes, size) < 0) {
        fprintf(stderr, "SDL audio: %s\n", SDL_GetError()); exit(2);
    }
}
static void audio_shutdown(void) {
    if (native_audio) {
        struct m8_pcm_stats stats;
        (void)m8_pcm_close(); m8_pcm_get_stats(&stats);
        fprintf(stderr, "SM64_AUDIO delivered=%llu dropped=%llu error=%d pending_overflow=%llu\n",
                (unsigned long long)stats.delivered_frames, (unsigned long long)stats.dropped_frames, stats.error,
                (unsigned long long)pcm_overflow);
    }
    native_audio = 0;
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    audio_device = 0;
}
struct AudioAPI audio_sdl = {audio_init_adapter, audio_buffered, audio_desired, audio_play, audio_shutdown};

/* Do not import the FunKey process snapshot/poweroff implementation. */
void savestate_check(void) {}
void savestate_request_save(int slot) { (void)slot; }
void savestate_request_load(int slot) { (void)slot; }
void savestate_get_name(int slot, char *name) { (void)slot; if (name) *name = 0; }
