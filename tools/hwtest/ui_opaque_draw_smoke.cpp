#define SDL_MAIN_HANDLED
#include "Renderer.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace lilsmoke;
static int checks = 0;
static SDL_Surface *activeSurface = nullptr;
static void check(bool ok, const char *label) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
    ++checks;
}
namespace lilsmoke {
struct RendererTestAccess {
    static void attach(Renderer &r, SDL_Surface *surface) {
        r.renderer_ = SDL_CreateSoftwareRenderer(surface);
        check(r.renderer_ != nullptr, "surface software renderer");
        r.width_ = r.outputWidth_ = surface->w;
        r.height_ = r.outputHeight_ = surface->h;
        activeSurface = surface;
    }
};
}
struct Rect { int x, y, w, h; uint32_t color; bool outline; };
static void reference(Renderer &r, const Rect &rect) {
    auto *sdl = static_cast<SDL_Renderer *>(r.sdlRenderer());
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, rect.color >> 24, rect.color >> 16, rect.color >> 8, rect.color);
    SDL_Rect box{rect.x, rect.y, rect.w, rect.h};
    if (rect.outline) SDL_RenderDrawRect(sdl, &box);
    else SDL_RenderFillRect(sdl, &box);
}
static void optimized(Renderer &r, const Rect &rect) {
    if (rect.outline) r.drawRect(rect.x, rect.y, rect.w, rect.h, rect.color);
    else r.fillRect(rect.x, rect.y, rect.w, rect.h, rect.color);
}
static std::vector<uint32_t> pixels(Renderer &r) {
    std::vector<uint32_t> result(static_cast<size_t>(r.outputWidth()) * r.outputHeight());
    check(SDL_RenderFlush(static_cast<SDL_Renderer *>(r.sdlRenderer())) == 0, "flush reference pixels");
    SDL_Surface *copy = SDL_ConvertSurfaceFormat(activeSurface, SDL_PIXELFORMAT_ARGB8888, 0);
    check(copy != nullptr, "copy complete surface regardless of viewport");
    for (int y = 0; y < copy->h; ++y)
        std::memcpy(result.data() + static_cast<size_t>(y) * copy->w,
                    static_cast<const uint8_t *>(copy->pixels) + static_cast<size_t>(y) * copy->pitch,
                    static_cast<size_t>(copy->w) * 4);
    SDL_FreeSurface(copy);
    return result;
}
static void verify(uint32_t format) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 257, 193, 32, format);
    check(surface != nullptr, "test surface");
    {
        Renderer r;
        RendererTestAccess::attach(r, surface);
        std::vector<Rect> rectangles = {
            {0, 0, 257, 193, 0x337755FF, false}, {-13, 17, 38, 51, 0xFF2050FF, false},
            {250, 188, 50, 70, 0x5080FFFF, false}, {16, 34, 100, 51, 0xD0406080, false},
            {24, 39, 18, 13, 0x00FF0000, false}, {8, 7, 200, 180, 0xFFD080FF, true},
            {-1, -1, 33, 41, 0x00204040, true}, {30, 40, 1, 1, 0xFFFFFFFF, true},
            {50, 60, 0, 0, 0xFFFFFFFF, false},
        };
        uint32_t random = 9781;
        for (int i = 0; i < 150; ++i) {
            random = random * 1664525U + 1013904223U;
            int x = static_cast<int>(random % 290) - 15;
            random = random * 1664525U + 1013904223U;
            int y = static_cast<int>(random % 230) - 15;
            uint32_t color = (random & 0xFFFFFF00U) | (i % 3 ? 255U : random & 255U);
            rectangles.push_back({x, y, 1 + i % 57, 1 + i % 61, color, i % 2 != 0});
        }
        for (bool clipped : {false, true}) {
            for (Uint8 alpha : {Uint8(0), Uint8(17), Uint8(255)}) {
                auto reset = [&]() {
                    SDL_RenderFlush(static_cast<SDL_Renderer *>(r.sdlRenderer()));
                    SDL_RenderSetViewport(static_cast<SDL_Renderer *>(r.sdlRenderer()), nullptr);
                    SDL_RenderSetClipRect(static_cast<SDL_Renderer *>(r.sdlRenderer()), nullptr);
                    SDL_SetClipRect(surface, nullptr);
                    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 18, 37, 56, alpha));
                    if (clipped) {
                        SDL_Rect viewport{5, 6, 240, 180};
                        SDL_RenderSetViewport(static_cast<SDL_Renderer *>(r.sdlRenderer()), &viewport);
                        r.setClipRect(17, 19, 137, 111);
                    }
                };
                reset();
                for (const Rect &rect : rectangles) reference(r, rect);
                const auto expected = pixels(r);
                reset();
                for (const Rect &rect : rectangles) optimized(r, rect);
                const auto actual = pixels(r);
                if (actual != expected) {
                    for (size_t i = 0; i < actual.size(); ++i) {
                        if (actual[i] == expected[i]) continue;
                        std::fprintf(stderr, "mismatch format=%s clipped=%d base_alpha=%u x=%zu y=%zu expected=%08x actual=%08x\n",
                            SDL_GetPixelFormatName(format), clipped, alpha, i % 257, i / 257, expected[i], actual[i]);
                        break;
                    }
                }
                check(actual == expected, "opaque/transparent/mixed rectangle pixels exactly match legacy blending");
                SDL_BlendMode mode;
                SDL_GetRenderDrawBlendMode(static_cast<SDL_Renderer *>(r.sdlRenderer()), &mode);
                check(mode == SDL_BLENDMODE_BLEND, "legacy follow-on draw blend state preserved");
            }
        }
    }
    SDL_FreeSurface(surface);
}
int main(int argc, char **) {
    SDL_SetMainReady();
    verify(SDL_PIXELFORMAT_ARGB8888);
    verify(SDL_PIXELFORMAT_RGB888);
    verify(SDL_PIXELFORMAT_ABGR8888);
    if (argc > 1) {
        std::printf("UI_OPAQUE_DRAW_SMOKE_OK checks=%d\n", checks);
        return 0;
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_ARGB8888);
    check(surface != nullptr, "benchmark surface");
    {
        Renderer r;
        RendererTestAccess::attach(r, surface);
        const int frames = 160;
        std::vector<double> oldTimes, newTimes;
        auto draw = [&](bool fast, int frame) {
            Rect rect{0, 0, 1280, 720, 0x203040FFU + static_cast<uint32_t>(frame % 100) * 0x10000U, false};
            if (fast) optimized(r, rect); else reference(r, rect);
            SDL_RenderFlush(static_cast<SDL_Renderer *>(r.sdlRenderer()));
        };
        for (int frame = 0; frame < frames + 5; ++frame) {
            for (bool fast : {false, true}) {
                const Uint64 began = SDL_GetPerformanceCounter();
                draw(fast, frame);
                const double elapsed = (SDL_GetPerformanceCounter() - began) * 1000.0 / SDL_GetPerformanceFrequency();
                if (frame >= 5) (fast ? newTimes : oldTimes).push_back(elapsed);
            }
        }
        std::sort(oldTimes.begin(), oldTimes.end()); std::sort(newTimes.begin(), newTimes.end());
        draw(false, 1); const auto expected = pixels(r);
        draw(true, 1); check(pixels(r) == expected, "full-size opaque benchmark matches exact pixels");
        std::printf("UI_OPAQUE_FILL_BENCH logical=1280x720 frames=%d legacy_median_ms=%.4f bulk_median_ms=%.4f legacy_p95_ms=%.4f bulk_p95_ms=%.4f\n",
            frames, oldTimes[frames / 2], newTimes[frames / 2], oldTimes[frames * 95 / 100], newTimes[frames * 95 / 100]);
    }
    SDL_FreeSurface(surface);
    std::printf("UI_OPAQUE_DRAW_SMOKE_OK checks=%d\n", checks);
}
