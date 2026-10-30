#define SDL_MAIN_HANDLED
#include "Renderer.h"
#include "Font.h"
#include "SystemView.h"
#include "GridView.h"
#include "ApplicationsView.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <time.h>
#include <vector>

using namespace lilsmoke;
static uint32_t authoredTime = 1000;
static std::vector<int32_t> damage;
static std::vector<uint32_t> front;
extern "C" Uint32 __wrap_SDL_GetTicks() { return authoredTime; }
static bool cpuClock() {
    static const bool enabled = std::getenv("C41_CPU_CLOCK") != nullptr;
    return enabled;
}
extern "C" Uint64 __real_SDL_GetPerformanceCounter();
extern "C" Uint64 __real_SDL_GetPerformanceFrequency();
extern "C" Uint64 __wrap_SDL_GetPerformanceCounter() {
    if (!cpuClock()) return __real_SDL_GetPerformanceCounter();
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) std::abort();
    return uint64_t(value.tv_sec) * 1000000000ULL + value.tv_nsec;
}
extern "C" Uint64 __wrap_SDL_GetPerformanceFrequency() {
    return cpuClock() ? 1000000000ULL : __real_SDL_GetPerformanceFrequency();
}
extern "C" SDL_Surface *__real_IMG_Load(const char *path);
static uint64_t decodedImages = 0;
extern "C" SDL_Surface *__wrap_IMG_Load(const char *path) {
    ++decodedImages;
    const auto began = SDL_GetPerformanceCounter();
    auto *surface = __real_IMG_Load(path);
    if (std::getenv("C41_DECODE_PROFILE"))
        std::fprintf(stderr, "DECODE ticks=%u ms=%.3f bytes=%zu path=%s\n", authoredTime,
            (SDL_GetPerformanceCounter() - began) * 1000.0 / SDL_GetPerformanceFrequency(),
            surface ? size_t(surface->pitch) * surface->h : 0, path);
    return surface;
}
static int accept(const void *, unsigned, unsigned, unsigned, const int32_t *rects, unsigned count) {
    damage.assign(rects, rects + count * 4); return 1;
}
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
namespace lilsmoke {
struct RendererTestAccess {
    template<class R> static auto bind(R &r, SDL_Surface *s, int) -> decltype(r.softwareSurface_ = s, void()) { r.softwareSurface_ = s; }
    template<class R> static void bind(R &, SDL_Surface *, ...) {}
    static bool init(Renderer &r, SDL_Surface *s) {
        r.width_ = r.outputWidth_ = s->w; r.height_ = r.outputHeight_ = s->h;
        r.renderer_ = SDL_CreateSoftwareRenderer(s);
        r.softwareWindowRenderer_ = true; r.profilePresent_ = true;
        r.nativePresentDamage_ = accept; bind(r, s, 0);
        return r.renderer_ != nullptr;
    }
    static bool present(Renderer &r, SDL_Surface *s) { r.presentStats_ = {}; return r.presentNativeSurface(s); }
    static size_t images(const Renderer &r) { return r.textureCacheBytes_; }
    static size_t text(const Renderer &r) { return r.textCacheBytes_; }
    static bool profiling(const Renderer &r) { return r.profilePresent_; }
#ifdef C41_RENDERER_UNITS
    static size_t derived(const Renderer &r) { return r.scaledCache_.size(); }
    static bool fast(Renderer &r, SDL_Texture *texture, const SDL_Rect &rect) {
        return r.drawSoftwareScaled(texture, rect.x, rect.y, rect.w, rect.h);
    }
    static void accounting(const Renderer &r) {
        size_t native = 0, scaled = 0, images = 0;
        for (const auto &entry : r.textureCache_) {
            native += entry.second.bytes;
            if (entry.second.texture) {
                ++images;
                const auto link = r.imageEntries_.find(entry.second.texture);
                check(link != r.imageEntries_.end() && link->second == &entry.second, "image owner index");
            }
        }
        check(images == r.imageEntries_.size(), "stale image owner index");
        for (const auto &entry : r.scaledCache_) scaled += entry.bytes;
        check(scaled == r.scaledCacheBytes_ && native + scaled == r.textureCacheBytes_, "cache accounting");
        check(r.scaledCache_.size() <= 16 && scaled <= r.textureCacheLimitBytes_ / 2, "derived cache bounds");
    }
#endif
};
struct ApplicationsTestAccess {
    static void populate(ApplicationsView &v, const std::string &path) {
        for (int i = 0; i < 18; ++i) v.apps_.push_back({"Application " + std::to_string(i), "1.0.0", path});
    }
    static void installer(ApplicationInstallerView &v, const std::string &path) {
        v.entered_ = true; v.state_ = ApplicationInstallerView::State::Ready;
        v.name_ = "Software Renderer Fixture"; v.version_ = "1.0.0"; v.icon_ = path;
    }
};
}
static double ms(uint64_t t) { return t * 1000.0 / SDL_GetPerformanceFrequency(); }
static void frame(View &v, Renderer &r, Font &font, const Theme &theme, SDL_Surface *s, const char *phase, int n) {
    const auto start = SDL_GetPerformanceCounter();
    v.render(r, font, theme);
    check(SDL_RenderFlush(static_cast<SDL_Renderer *>(r.sdlRenderer())) == 0, "render flush");
    const auto rendered = SDL_GetPerformanceCounter();
    check(RendererTestAccess::present(r, s), "native mock present");
    const auto presented = SDL_GetPerformanceCounter();
    for (size_t i = 0; i < damage.size(); i += 4)
        for (int y = damage[i+1]; y < damage[i+1] + damage[i+3]; ++y)
            std::memcpy(front.data() + size_t(y) * s->w + damage[i],
                static_cast<uint8_t *>(s->pixels) + y * s->pitch + damage[i] * 4, damage[i+2] * 4);
    uint64_t hash = 14695981039346656037ULL;
    for (int y = 0; y < s->h; ++y) for (int x = 0; x < s->w; ++x) {
        uint32_t p; std::memcpy(&p, static_cast<uint8_t *>(s->pixels) + y * s->pitch + x * 4, 4);
        check(((front[size_t(y) * s->w + x] ^ p) & 0xFFFFFF) == 0, "damage lost pixels");
        hash = (hash ^ (p & 0xFFFFFF)) * 1099511628211ULL;
    }
    std::printf("%s,%d,%u,%.6f,%.6f,%.6f,%zu,%zu,%u,%016llx\n", phase, n, authoredTime,
        ms(rendered-start), ms(presented-rendered), ms(r.presentStats().compareTicks),
        RendererTestAccess::images(r), RendererTestAccess::text(r), r.presentStats().rects,
        static_cast<unsigned long long>(hash));
}
int main(int argc, char **argv) {
    try {
        check(argc == 6, "usage: candidate41-perf THEME WIDTH HEIGHT ICON FONT");
        const int width = std::stoi(argv[2]), height = std::stoi(argv[3]);
        SDL_SetMainReady(); check(SDL_Init(0) == 0, "SDL init");
        setenv("LILSMOKE_DRAW_CLOCK", "0", 1); setenv("LILSMOKE_SHOW_BATTERY", "off", 1);
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
            SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGB888), SDL_FreeSurface);
        check(surface != nullptr, "surface");
        Renderer r; check(RendererTestAccess::init(r, surface.get()), "software renderer");
        if (const char *cache = std::getenv("C41_CACHE_MB"))
            r.setTextureCacheLimitMB(std::stoi(cache));
        Font f; check(f.load(argv[5], 16), "font");
        front.resize(size_t(width) * height);
        std::puts("phase,frame,ticks,render_ms,present_ms,compare_ms,image_bytes,text_bytes,rects,hash");
        const int step = std::getenv("C41_SHORT") ? 100 : 20;
        const std::string root = argv[1];
        Theme theme; check(theme.loadSet(root, "snes", "Super Nintendo"), "theme load");
        SystemView system({{"snes", "Super Nintendo", 77}, {"nes", "Nintendo", 65},
            {"gba", "Game Boy Advance", 110}, {"md", "Mega Drive", 84}, {"__applications", "Applications", 18}}, {}, &root);
        system.onEnter();
        frame(system, r, f, theme, surface.get(), "system-cold", 0);
        authoredTime = 2000;
        for (const auto style : {"slide", "fade", "instant"}) {
            setenv("LILSMOKE_THEME_TRANSITION", style, 1);
            for (int cycle = 0; cycle < 3; ++cycle) {
                const uint32_t base = authoredTime + 1000;
                authoredTime = base; system.handleInput(InputButton::Right, InputEvent::Pressed);
                for (int t = 0; t <= 600; t += step) {
                    authoredTime = base + t;
                    frame(system, r, f, theme, surface.get(), style, cycle * 1000 + t);
                }
            }
        }
        for (int i = 0; i < 8; ++i) {
            authoredTime += 20; frame(system, r, f, theme, surface.get(), "system-idle", i);
        }
        std::vector<ROM> games(20); std::vector<const ROM *> refs;
        for (int i = 0; i < 20; ++i) {
            games[i].name = "Game " + std::to_string(i); games[i].system = "snes";
            games[i].coverPath = argv[4];
            games[i].description = "Production software renderer description wrapping and timed artwork fade regression.";
            refs.push_back(&games[i]);
        }
        GridView grid(refs, {}, "SNES", theme); grid.onEnter();
        for (int i = 0; i < 20; ++i) {
            authoredTime += step; if (i % 5 == 0) grid.setSelectedIndex(i / 5);
            frame(grid, r, f, theme, surface.get(), "grid", i);
        }
        ApplicationsView apps; ApplicationsTestAccess::populate(apps, argv[4]);
        for (int i = 0; i < 12; ++i) {
            authoredTime += step;
            if (i > 0 && i % 3 == 0) apps.handleInput(InputButton::Right, InputEvent::Pressed);
            frame(apps, r, f, theme, surface.get(), i == 0 ? "apps-cold" : "apps", i);
        }
        apps.handleInput(InputButton::Y, InputEvent::Pressed);
        frame(apps, r, f, theme, surface.get(), "apps-dialog", 0);
        ApplicationInstallerView installer("/nonexistent-fixture");
        ApplicationsTestAccess::installer(installer, argv[4]);
        for (int i = 0; i < 8; ++i) {
            authoredTime += step;
            frame(installer, r, f, theme, surface.get(), i == 0 ? "installer-cold" : "installer", i);
        }
        std::fprintf(stderr, "C41_PRODUCTION_VIEWS_OK width=%d height=%d\n", width, height);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL %s\n", e.what()); return 1; }
}
