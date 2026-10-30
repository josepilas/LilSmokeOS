#define SDL_MAIN_HANDLED
#include "Cursor.h"
#include "Font.h"
#include "Renderer.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>

using namespace lilsmoke;
static unsigned checks = 0, nativeCalls = 0, sdlCalls = 0;
static bool failNext = false;
static Renderer *liveRenderer = nullptr;
static std::vector<uint32_t> front;
static std::vector<int32_t> lastRects;
static unsigned frontWidth = 0, frontHeight = 0;

static void check(bool ok, const char *label) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
    ++checks;
}
namespace lilsmoke {
struct RendererTestAccess {
    static SDL_Window *window(Renderer &r) { return r.window_; }
    static size_t shadowBytes(const Renderer &r) { return r.damageShadowBytes_; }
    static bool valid(const Renderer &r) { return r.damageShadowValid_; }
    static bool resolved(const Renderer &r) { return r.nativePresentDamage_ != nullptr; }
    static bool synthetic(Renderer &r, SDL_Surface *surface) {
        r.presentStats_ = {};
        r.nativeDamageFailed_ = false;
        return r.presentNativeSurface(surface);
    }
    static void software(Renderer &r, bool enabled) { r.softwareWindowRenderer_ = enabled; }
    static void forceFullFrame(Renderer &r) { r.damageShadowValid_ = false; }
    static void postponeRetry(Renderer &r) { r.nativeRetryAtMs_ = SDL_GetTicks64() + 60000; }
    static void expireRetry(Renderer &r) { r.nativeRetryAtMs_ = 0; }
    static unsigned retryDelay(const Renderer &r) { return r.nativeRetryDelayMs_; }
};
}
static uint32_t sourcePixel(const void *pixels, unsigned pitch, unsigned x, unsigned y) {
    uint32_t result;
    std::memcpy(&result, static_cast<const uint8_t *>(pixels) + static_cast<size_t>(y) * pitch + x * 4U, 4);
    return result | 0xFF000000U;
}
static void resizeFront(unsigned width, unsigned height) {
    if (frontWidth == width && frontHeight == height) return;
    frontWidth = width; frontHeight = height;
    front.assign(static_cast<size_t>(width) * height, 0xFFFF00FFU);
}

// Export with -rdynamic to exercise the real dlsym bridge, without any framebuffer IO.
extern "C" int lilsmoke_present_damage(const void *pixels, unsigned width, unsigned height,
                                       unsigned pitch, const int32_t *rects, unsigned count) {
    ++nativeCalls;
    check(pixels && width && height && pitch >= width * 4U && count <= 2048,
          "native ABI dimensions/pitch/count");
    check(count == 0 || rects, "native ABI rectangle pointer");
    lastRects.assign(rects, rects + count * 4U);
    resizeFront(width, height);
    if (failNext) {
        failNext = false;
        front[0] = 0xFFFF00FFU;
        errno = EIO;
        return 0;
    }
    for (unsigned i = 0; i < count; ++i) {
        const int32_t *r = rects + i * 4U;
        check(r[0] >= 0 && r[1] >= 0 && r[2] > 0 && r[3] > 0 &&
              static_cast<uint64_t>(r[0]) + r[2] <= width &&
              static_cast<uint64_t>(r[1]) + r[3] <= height, "all damage rectangles in bounds");
        for (int y = r[1]; y < r[1] + r[3]; ++y)
            for (int x = r[0]; x < r[0] + r[2]; ++x)
                front[static_cast<size_t>(y) * width + x] = sourcePixel(pixels, pitch, x, y);
    }
    return 1;
}
extern "C" void __real_SDL_RenderPresent(SDL_Renderer *);
extern "C" void __wrap_SDL_RenderPresent(SDL_Renderer *renderer) {
    ++sdlCalls;
    __real_SDL_RenderPresent(renderer);
    if (!liveRenderer || renderer != liveRenderer->sdlRenderer()) return;
    SDL_Surface *s = SDL_GetWindowSurface(RendererTestAccess::window(*liveRenderer));
    check(s && s->format->BytesPerPixel == 4, "mock SDL fallback surface");
    resizeFront(s->w, s->h);
    for (unsigned y = 0; y < frontHeight; ++y)
        for (unsigned x = 0; x < frontWidth; ++x)
            front[static_cast<size_t>(y) * frontWidth + x] = sourcePixel(s->pixels, s->pitch, x, y);
}
static void frontMatches(SDL_Surface *s) {
    check(s && frontWidth == static_cast<unsigned>(s->w) && frontHeight == static_cast<unsigned>(s->h),
          "front dimensions match frame");
    for (unsigned y = 0; y < frontHeight; ++y)
        for (unsigned x = 0; x < frontWidth; ++x)
            if (front[static_cast<size_t>(y) * frontWidth + x] != sourcePixel(s->pixels, s->pitch, x, y)) {
                std::fprintf(stderr, "pixel mismatch x=%u y=%u\n", x, y);
                check(false, "damage front equals full opaque frame");
            }
    ++checks;
}
static void frontMatches(Renderer &r) { frontMatches(SDL_GetWindowSurface(RendererTestAccess::window(r))); }
static void scene(Renderer &r, Font &font, CursorAtlas &cursor, int x, int y,
                   bool visible = true, bool hover = false, const char *clock = "12:34", int animation = 0) {
    r.clear(0x223344FF);
    r.fillRect(48, 80, 260, 64, hover ? 0x608F70FF : 0x303A42FF);
    font.render(r, "Native touchpad / mouse", 60, 98, 0xFFFFFFFF);
    font.render(r, clock, 1160, 12, 0xFFFFFFFF);
    r.fillRect(780 + animation, 600, 18, 18, 0xD09090A0);
    if (visible) cursor.draw(r, CursorKind::Normal, x, y, 32);
}
static void expose(Renderer &r) {
    SDL_Event event{};
    event.type = SDL_WINDOWEVENT;
    event.window.windowID = SDL_GetWindowID(RendererTestAccess::window(r));
    event.window.event = SDL_WINDOWEVENT_EXPOSED;
    check(SDL_PushEvent(&event) == 1, "isolated dummy window exposure event");
}
static void syntheticCases(Renderer &r) {
    constexpr int width = 137, height = 73, pitch = width * 4 + 20;
    std::vector<uint8_t> memory(static_cast<size_t>(pitch) * height, 0x9A);
    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(memory.data(), width, height, 32, pitch,
                                             0xFF0000U, 0xFF00U, 0xFFU, 0xFF000000U);
    check(s != nullptr, "padded ARGB source");
    check(RendererTestAccess::synthetic(r, s), "padded first frame accepted");
    frontMatches(s);
    check(r.presentStats().fullFrame && r.presentStats().nativeBytes == width * height * 4U,
          "shadow packs rows without retaining pitch padding");
    for (int y = 20; y < 25; ++y) {
        uint32_t pixel = 0x10203040;
        std::memcpy(memory.data() + y * pitch + 132 * 4, &pixel, 4);
    }
    check(RendererTestAccess::synthetic(r, s), "partial last horizontal run accepted");
    frontMatches(s);
    check(lastRects == std::vector<int32_t>({128, 20, 9, 5}), "edge tile clipped and matching row runs merge vertically");
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) memory[static_cast<size_t>(y) * pitch + x * 4 + 3] ^= 0xAA;
    check(RendererTestAccess::synthetic(r, s), "alpha-only frame accepted");
    check(lastRects.empty() && r.presentStats().nativeBytes == 0, "opaque output ignores alpha-only differences");
    frontMatches(s);
    memory[width * 4] ^= 1;
    check(RendererTestAccess::synthetic(r, s) && lastRects.empty(), "pitch padding is not visible damage");
    uint32_t random = 12345;
    for (int frame = 0; frame < 80; ++frame) {
        for (int i = 0; i < 12; ++i) {
            random = random * 1664525U + 1013904223U;
            int x = random % width;
            random = random * 1664525U + 1013904223U;
            int y = random % height;
            std::memcpy(memory.data() + static_cast<size_t>(y) * pitch + x * 4, &random, 4);
        }
        check(RendererTestAccess::synthetic(r, s), "random sparse frame accepted");
        frontMatches(s);
    }
    SDL_PixelFormat invalid = *s->format;
    SDL_Surface bad = *s;
    bad.format = &invalid;
    invalid.Rmask = 0xFFU; invalid.Bmask = 0xFF0000U;
    unsigned calls = nativeCalls;
    check(!RendererTestAccess::synthetic(r, &bad) && nativeCalls == calls, "ABGR rejected before native API");
    invalid = *s->format; invalid.Amask = 0xF0000000U;
    check(!RendererTestAccess::synthetic(r, &bad) && nativeCalls == calls, "non-eight-bit alpha rejected");
    bad = *s; bad.w = 1921;
    check(!RendererTestAccess::synthetic(r, &bad) && nativeCalls == calls, "width cap rejected before allocation/read");
    bad = *s; bad.h = 1081;
    check(!RendererTestAccess::synthetic(r, &bad) && nativeCalls == calls, "height cap rejected before allocation/read");
    bad = *s; bad.pitch = width * 4 - 1;
    check(!RendererTestAccess::synthetic(r, &bad) && nativeCalls == calls, "short pitch rejected");
    bad = *s; bad.pitch = 16388;
    check(!RendererTestAccess::synthetic(r, &bad) && nativeCalls == calls, "source pitch cap rejected");
    SDL_Surface *rgb = SDL_CreateRGBSurfaceWithFormat(0, 137, 73, 32, SDL_PIXELFORMAT_RGB888);
    check(rgb && RendererTestAccess::synthetic(r, rgb), "XRGB source accepted");
    frontMatches(rgb);
    check(r.presentStats().fullFrame, "surface replacement forces full synchronization");
    check(RendererTestAccess::shadowBytes(r) == width * height * 4U, "bounded replacement shadow size");
    SDL_Surface *huge = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_ARGB8888);
    check(huge && RendererTestAccess::synthetic(r, huge), "maximum supported dimensions accepted");
    check(RendererTestAccess::shadowBytes(r) == 8294400 && RendererTestAccess::shadowBytes(r) <= 8U * 1024U * 1024U,
          "maximum shadow remains under eight MiB");
    frontMatches(huge);
    SDL_FreeSurface(huge); SDL_FreeSurface(rgb); SDL_FreeSurface(s);
}
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    SDL_SetMainReady();
    unsetenv("LILSMOKE_UI_NATIVE_DAMAGE");
    {
        Renderer r;
        check(r.init(1280, 720), "software renderer");
        liveRenderer = &r;
        check(RendererTestAccess::resolved(r), "native symbol resolved by production dlsym");
        Font font;
        check(font.load(argv[1], 24), "fixture font");
        CursorAtlas cursor;
        check(cursor.load(r, argv[2]), "real cursor atlas");
        scene(r, font, cursor, 200, 220);
        r.present(); frontMatches(r);
        check(r.presentStats().fullFrame && r.presentStats().nativeBytes == 3686400 && sdlCalls == 0,
              "first frame fully synchronized without SDL present");
        scene(r, font, cursor, 205, 223);
        r.present(); frontMatches(r);
        check(!r.presentStats().fullFrame && r.presentStats().nativeBytes < 65536,
              "cursor motion copies bounded old/new regions without trails");
        scene(r, font, cursor, 205, 223);
        r.present(); frontMatches(r);
        check(r.presentStats().native && r.presentStats().rects == 0 && r.presentStats().copyRows == 0,
              "identical scene count-zero native success");
        scene(r, font, cursor, 205, 223, false);
        r.present(); frontMatches(r);
        check(r.presentStats().nativeBytes > 0 && !r.presentStats().fullFrame, "invisible cursor restores underlying pixels");
        scene(r, font, cursor, 1000, 420, true, true, "12:35", 19);
        r.present(); frontMatches(r);
        check(!r.presentStats().fullFrame, "hover clock animation and cursor changes covered together");
        r.clear(0xBB5522FF); r.present(); frontMatches(r);
        check(r.presentStats().fullFrame && r.presentStats().rects == 1, "dense changes use one full frame");
        r.clear(0x000000FF); r.present();
        for (int y = 0; y < 140; y += 2)
            for (int x = 0; x < 1280; x += 128) r.fillRect(x, y, 1, 1, 0xFFFFFFFF);
        r.present(); frontMatches(r);
        check(r.presentStats().fullFrame && r.presentStats().rects == 1, "fragmentation cap promotes full frame");
        failNext = true;
        scene(r, font, cursor, 320, 300);
        unsigned fallbackBefore = sdlCalls;
        r.present(); frontMatches(r);
        check(!r.presentStats().native && r.presentStats().fallback && sdlCalls == fallbackBefore + 1,
              "native rejection executes real SDL fallback in same frame");
        check(!RendererTestAccess::valid(r) && RendererTestAccess::shadowBytes(r) == 0,
              "rejected frame never commits and releases shadow");
        check(r.presentStats().nativeAttempted && r.presentStats().nativeResult == 0 &&
              r.presentStats().nativeError == EIO && RendererTestAccess::retryDelay(r) == 100,
              "first rejection records fresh errno and schedules bounded retry");
        RendererTestAccess::postponeRetry(r);
        unsigned nativeBefore = nativeCalls;
        scene(r, font, cursor, 330, 300); r.present(); frontMatches(r);
        check(nativeCalls == nativeBefore && r.presentStats().fallback, "failed API is not retried every frame");
        RendererTestAccess::expireRetry(r);
        scene(r, font, cursor, 330, 300); r.present(); frontMatches(r);
        check(nativeCalls == nativeBefore + 1 && r.presentStats().native && r.presentStats().fullFrame &&
              RendererTestAccess::retryDelay(r) == 0, "transient first-frame failure recovers without window event");
        for (unsigned failure = 0; failure < 7; ++failure) {
            failNext = true;
            RendererTestAccess::expireRetry(r);
            scene(r, font, cursor, 331 + failure, 300); r.present(); frontMatches(r);
            check(RendererTestAccess::retryDelay(r) == std::min(100U << failure, 2000U),
                  "persistent API rejection uses capped exponential backoff");
        }
        expose(r);
        scene(r, font, cursor, 330, 300); r.present(); frontMatches(r);
        check(r.presentStats().native && r.presentStats().fullFrame && RendererTestAccess::retryDelay(r) == 0,
              "exposure retries with full synchronization and resets backoff");
        r.setSafeAreaPercent(5);
        check(RendererTestAccess::shadowBytes(r) == 0, "presentation change releases shadow before redraw");
        scene(r, font, cursor, 310, 310); r.present(); frontMatches(r);
        check(r.presentStats().fullFrame, "TV safe area redraw is fully synchronized");
        r.setSafeAreaPercent(0);
        RendererTestAccess::software(r, false);
        nativeBefore = nativeCalls;
        scene(r, font, cursor, 320, 320); r.present(); frontMatches(r);
        check(r.presentStats().fallback && nativeCalls == nativeBefore, "non-software backend bypasses native path");
        RendererTestAccess::software(r, true);
        scene(r, font, cursor, 320, 320); r.present(); frontMatches(r);
        check(r.presentStats().fullFrame, "fallback invalidates native shadow");
        syntheticCases(r);
        scene(r, font, cursor, 320, 320); r.present(); frontMatches(r);
        check(r.presentStats().fullFrame, "returning from a changed surface synchronizes real window");
        const int frames = 240;
        uint64_t fullBytes = 0;
        const Uint64 fullBegan = SDL_GetPerformanceCounter();
        for (int frame = 0; frame < frames; ++frame) {
            scene(r, font, cursor, 320 + frame * 3 % 720, 200 + frame % 140);
            RendererTestAccess::forceFullFrame(r);
            r.present();
            check(r.presentStats().native && r.presentStats().fullFrame, "full-copy control stays native");
            fullBytes += r.presentStats().nativeBytes;
        }
        const double fullElapsedMs = (SDL_GetPerformanceCounter() - fullBegan) * 1000.0 / SDL_GetPerformanceFrequency();
        frontMatches(r);
        check(fullBytes == static_cast<uint64_t>(frames) * 1280 * 720 * 4, "full-copy control byte count measured");
        uint64_t bytes = 0, rows = 0, rectangles = 0;
        const Uint64 began = SDL_GetPerformanceCounter();
        for (int frame = 0; frame < frames; ++frame) {
            scene(r, font, cursor, 320 + frame * 3 % 720, 200 + frame % 140);
            r.present();
            check(r.presentStats().native && !r.presentStats().fallback, "benchmark stays native");
            bytes += r.presentStats().nativeBytes; rows += r.presentStats().copyRows; rectangles += r.presentStats().rects;
        }
        const double elapsedMs = (SDL_GetPerformanceCounter() - began) * 1000.0 / SDL_GetPerformanceFrequency();
        frontMatches(r);
        check(bytes < fullBytes / 50, "simulated pointer motion uses under two percent full-frame copy bytes");
        std::printf("UI_DAMAGE_COPY_BENCH frames=%d full_copy_bytes=%llu dirty_bytes=%llu copy_rows=%llu rects=%llu host_full_fixture_ms=%.3f host_dirty_fixture_ms=%.3f\n",
            frames, static_cast<unsigned long long>(fullBytes), static_cast<unsigned long long>(bytes),
            static_cast<unsigned long long>(rows), static_cast<unsigned long long>(rectangles), fullElapsedMs, elapsedMs);
        liveRenderer = nullptr;
    }
    setenv("LILSMOKE_UI_NATIVE_DAMAGE", "0", 1);
    {
        Renderer r;
        check(r.init(1280, 720), "disabled native renderer");
        liveRenderer = &r;
        unsigned before = nativeCalls;
        r.clear(0x435689FF); r.present(); frontMatches(r);
        check(!RendererTestAccess::resolved(r) && r.presentStats().fallback && nativeCalls == before,
              "disabled/absent API uses SDL with no shadow");
        check(RendererTestAccess::shadowBytes(r) == 0, "fallback does not allocate frame shadow");
        liveRenderer = nullptr;
    }
    unsetenv("LILSMOKE_UI_NATIVE_DAMAGE");
    std::printf("UI_DAMAGE_SMOKE_OK checks=%u native_calls=%u sdl_calls=%u\n", checks, nativeCalls, sdlCalls);
}
