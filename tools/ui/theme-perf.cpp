#define SDL_MAIN_HANDLED
#include "Font.h"
#include "GridView.h"
#include "Renderer.h"
#include "SystemView.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lilsmoke;
static uint32_t now = 1000;
static uint64_t decodes, decodeTicks, measures, measureTicks, rasters, rasterTicks;
static uint64_t copies, scaledCopies, rotatedCopies;
static std::vector<uint32_t> front;
static std::vector<int32_t> rects;
static SDL_Window *window;
static bool rejectNext = false;
static void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
extern "C" Uint32 __wrap_SDL_GetTicks() { return now; }
#ifndef PERF_TIMING_ONLY
extern "C" SDL_Surface *__real_IMG_Load(const char *);
extern "C" SDL_Surface *__wrap_IMG_Load(const char *path) {
    const auto start = SDL_GetPerformanceCounter();
    SDL_Surface *s = __real_IMG_Load(path);
    decodeTicks += SDL_GetPerformanceCounter() - start; ++decodes;
    std::fprintf(stderr, "DECODE %s %dx%d\n", path, s ? s->w : 0, s ? s->h : 0);
    return s;
}
extern "C" int __real_TTF_SizeUTF8(TTF_Font *, const char *, int *, int *);
extern "C" int __wrap_TTF_SizeUTF8(TTF_Font *f, const char *s, int *w, int *h) {
    const auto start = SDL_GetPerformanceCounter();
    int result = __real_TTF_SizeUTF8(f, s, w, h);
    measureTicks += SDL_GetPerformanceCounter() - start; ++measures;
    return result;
}
extern "C" SDL_Surface *__real_TTF_RenderUTF8_Blended(TTF_Font *, const char *, SDL_Color);
extern "C" SDL_Surface *__wrap_TTF_RenderUTF8_Blended(TTF_Font *f, const char *s, SDL_Color c) {
    const auto start = SDL_GetPerformanceCounter();
    auto *result = __real_TTF_RenderUTF8_Blended(f, s, c);
    rasterTicks += SDL_GetPerformanceCounter() - start; ++rasters;
    return result;
}
extern "C" int __real_SDL_RenderCopy(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *);
extern "C" int __wrap_SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d) {
    int w = 0, h = 0; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
    ++copies; scaledCopies += d && (d->w != (s ? s->w : w) || d->h != (s ? s->h : h));
    return __real_SDL_RenderCopy(r, t, s, d);
}
extern "C" int __real_SDL_RenderCopyEx(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *, double, const SDL_Point *, SDL_RendererFlip);
extern "C" int __wrap_SDL_RenderCopyEx(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d, double a, const SDL_Point *c, SDL_RendererFlip f) {
    ++rotatedCopies;
    return __real_SDL_RenderCopyEx(r, t, s, d, a, c, f);
}
#endif
extern "C" int lilsmoke_present_damage(const void *, unsigned w, unsigned h, unsigned, const int32_t *r, unsigned n) {
    rects.assign(r, r + n * 4);
    if (front.size() != static_cast<size_t>(w) * h) front.assign(static_cast<size_t>(w) * h, 0);
    if (rejectNext) { rejectNext = false; return 0; }
    return 1;
}
namespace lilsmoke {
struct RendererTestAccess {
    static SDL_Window *windowOf(Renderer &r) { return r.window_; }
    static size_t imageBytes(Renderer &r) { return r.textureCacheBytes_; }
    static size_t textBytes(Renderer &r) { return r.textCacheBytes_; }
    static void expireRetry(Renderer &r) { r.nativeRetryAtMs_ = 0; }
};
}
static double ms(uint64_t ticks) { return ticks * 1000.0 / SDL_GetPerformanceFrequency(); }
static void frame(View &v, Renderer &r, Font &f, const Theme &theme, const char *phase, int number) {
    decodes = decodeTicks = measures = measureTicks = rasters = rasterTicks = 0;
    copies = scaledCopies = rotatedCopies = 0;
    const auto start = SDL_GetPerformanceCounter();
    v.render(r, f, theme);
    check(SDL_RenderFlush(static_cast<SDL_Renderer *>(r.sdlRenderer())) == 0, "flush");
    const auto rendered = SDL_GetPerformanceCounter();
    r.present();
    const auto presented = SDL_GetPerformanceCounter();
    SDL_Surface *s = SDL_GetWindowSurface(window);
    check(s && s->format->BytesPerPixel == 4, "surface");
    // Verify native damage reconstructs every production frame, outside timed regions.
    if (!r.presentStats().native) rects = {0, 0, s->w, s->h};
    for (size_t i = 0; i < rects.size(); i += 4) {
        check(rects[i] >= 0 && rects[i+1] >= 0 && rects[i+2] > 0 && rects[i+3] > 0 &&
              rects[i] + rects[i+2] <= s->w && rects[i+1] + rects[i+3] <= s->h, "damage bounds");
        for (int y = rects[i+1]; y < rects[i+1] + rects[i+3]; ++y)
            for (int x = rects[i]; x < rects[i] + rects[i+2]; ++x) {
                uint32_t p; std::memcpy(&p, static_cast<uint8_t *>(s->pixels) + y * s->pitch + x * 4, 4);
                front[static_cast<size_t>(y) * s->w + x] = p & 0xFFFFFF;
            }
    }
    uint64_t hash = 14695981039346656037ULL;
    for (int y = 0; y < s->h; ++y) for (int x = 0; x < s->w; ++x) {
        uint32_t p; std::memcpy(&p, static_cast<uint8_t *>(s->pixels) + y * s->pitch + x * 4, 4);
        check(front[static_cast<size_t>(y) * s->w + x] == (p & 0xFFFFFF), "native damage lost pixels");
        hash = (hash ^ (p & 0xFFFFFF)) * 1099511628211ULL;
    }
    std::printf("%s\t%d\t%u\t%.6f\t%.6f\t%.6f\t%llu\t%.6f\t%llu\t%.6f\t%llu\t%.6f\t%llu\t%llu\t%llu\t%zu\t%zu\t%llu\t%u\t%016llx\n",
        phase, number, now, ms(rendered-start), ms(presented-rendered), ms(r.presentStats().compareTicks),
        (unsigned long long)decodes, ms(decodeTicks), (unsigned long long)measures, ms(measureTicks),
        (unsigned long long)rasters, ms(rasterTicks), (unsigned long long)copies,
        (unsigned long long)scaledCopies, (unsigned long long)rotatedCopies,
        RendererTestAccess::imageBytes(r), RendererTestAccess::textBytes(r),
        (unsigned long long)r.presentStats().nativeBytes, r.presentStats().rects, (unsigned long long)hash);
}
int main(int argc, char **argv) {
    try {
        check(argc == 5, "usage: theme-perf THEME FONT WIDTH HEIGHT");
        setenv("SDL_VIDEODRIVER", "dummy", 1); setenv("LILSMOKE_UI_WINDOWED", "1", 1);
        setenv("LILSMOKE_UI_HIDDEN", "1", 1); setenv("LILSMOKE_UI_PROFILE", "1", 1);
        setenv("LILSMOKE_DRAW_CLOCK", "0", 1); setenv("LILSMOKE_SHOW_BATTERY", "off", 1);
        setenv("LILSMOKE_THEME_TRANSITION", "slide", 1);
        SDL_SetMainReady();
        Renderer r; check(r.init(std::stoi(argv[3]), std::stoi(argv[4])), "renderer");
        window = RendererTestAccess::windowOf(r);
        Font font; check(font.load(argv[2], 16), "font");
        const std::string root = argv[1];
        Theme theme; check(theme.loadSet(root, "snes", "Super Nintendo"), "theme");
        SystemView system({{"snes", "Super Nintendo", 77}, {"nes", "Nintendo", 65},
            {"gba", "Game Boy Advance", 110}, {"md", "Mega Drive", 84}}, {}, &root);
        system.onEnter();
        std::puts("phase\tframe\ttime\trender_ms\tpresent_ms\tcompare_ms\tdecodes\tdecode_ms\tmeasures\tmeasure_ms\trasters\traster_ms\tcopies\tscaled\trotated\timage_bytes\ttext_bytes\tnative_bytes\trects\thash");
        frame(system, r, font, theme, "cold", 0);
        for (int i = 0; i < 24; ++i) {
            now = 2000 + i * 40;
            frame(system, r, font, theme, "idle", i);
        }
        for (int i = 0; i < 100; ++i) {
            now = 4000 + i * 40;
            if (i % 25 == 0) system.handleInput(InputButton::Right, InputEvent::Pressed);
            frame(system, r, font, theme, "system", i);
        }
        std::vector<ROM> roms(12); std::vector<const ROM *> refs;
        for (int i = 0; i < 12; ++i) {
            roms[i].system = "snes"; roms[i].name = "Performance Game " + std::to_string(i);
            roms[i].description = "Production layout with a longer game description for wrapping. "
                "The same text, authored times and original theme assets are used before and after. "
                "No reduced resolution, frame skipping or animation timing adjustment is permitted.";
            refs.push_back(&roms[i]);
        }
        GridView grid(refs, {}, "SNES", theme); grid.onEnter();
        for (int i = 0; i < 50; ++i) {
            now = 10000 + i * 40;
            if (i % 10 == 0) grid.setSelectedIndex(i / 10);
            frame(grid, r, font, theme, "grid", i);
        }
        // A failed native present must never commit a new shadow.
        rejectNext = true; now += 40; grid.setSelectedIndex(7);
        frame(grid, r, font, theme, "reject", 0);
        RendererTestAccess::expireRetry(r); now += 40;
        frame(grid, r, font, theme, "recover", 0);
        std::fprintf(stderr, "THEME_PERF_OK frames=177 full_pixel_damage_validation=1\n");
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL %s\n", e.what()); return 1; }
}
