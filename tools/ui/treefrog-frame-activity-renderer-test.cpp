// SPDX-License-Identifier: MPL-2.0
#define SDL_MAIN_HANDLED
#include "FrameActivityProfile.h"
#include "Renderer.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace lilsmoke;
static unsigned checks = 0;
static bool acceptFrame = true;
static std::vector<uint32_t> front;
static void check(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
static int present(const void *pixels, unsigned width, unsigned, unsigned pitch,
                   const int32_t *rects, unsigned count) {
    if (!acceptFrame) return 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto *r = rects + i * 4;
        for (int y = r[1]; y < r[1] + r[3]; ++y)
            std::memcpy(front.data() + size_t(y) * width + r[0],
                        static_cast<const uint8_t *>(pixels) + size_t(y) * pitch + r[0] * 4,
                        size_t(r[2]) * 4);
    }
    return 1;
}
namespace lilsmoke {
struct RendererTestAccess {
    static void init(Renderer &r, SDL_Surface *s) {
        r.width_ = r.outputWidth_ = s->w; r.height_ = r.outputHeight_ = s->h;
        r.renderer_ = SDL_CreateSoftwareRenderer(s);
        r.softwareSurface_ = s;
        r.nativePresentDamage_ = present;
        check(r.renderer_ != nullptr, "software renderer");
    }
    static bool submit(Renderer &r, SDL_Surface *s) {
        r.presentStats_ = {};
        check(SDL_RenderFlush(r.renderer_) == 0, "flush");
        return r.presentNativeSurface(s);
    }
    static void invalidate(Renderer &r) { r.resetDamageShadow(); }
};
}

static std::vector<uint64_t> scene(bool profiling, bool moving, int width, int height) {
    check(SDL_Init(0) == 0, "SDL init");
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGB888), SDL_FreeSurface);
    check(surface != nullptr, "surface");
    Renderer renderer;
    RendererTestAccess::init(renderer, surface.get());
    FrameActivityProfile observation(profiling ? 1000000 : 0);
    front.assign(size_t(width) * height, 0);
    std::vector<uint64_t> hashes;
    for (unsigned frame = 0; frame < 120; ++frame) {
        renderer.clear(0x186438FF);
        renderer.fillRect(0, 0, width, 16, 0xC03040FF); // Anchored scene, not a moving page.
        renderer.fillRect(24 + (moving ? frame : 0), 50, 20, 30, 0x4080E0FF);
        check(RendererTestAccess::submit(renderer, surface.get()), "native mock accepted");
        const auto &stats = renderer.presentStats();
        if (profiling) {
            const uint64_t tick = uint64_t(frame) * 16667;
            check(observation.record(tick, tick + 10, tick + 1000,
                                     frame % 20 == 0, stats.native, stats.fallback, stats.rects), "record renderer");
        }
        uint64_t hash = 14695981039346656037ULL;
        bool matched = true, anchored = true;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            uint32_t pixel;
            std::memcpy(&pixel, static_cast<const uint8_t *>(surface->pixels) + y * surface->pitch + x * 4, 4);
            pixel &= 0x00FFFFFFU;
            matched &= (front[size_t(y) * width + x] & 0x00FFFFFFU) == pixel;
            if (y < 16) anchored &= pixel == 0xC03040U;
            hash = (hash ^ pixel) * 1099511628211ULL;
        }
        check(matched, "damage lost pixels"); check(anchored, "stationary background moved");
        if (frame) check((hash != hashes.back()) == moving, "static/animation signature");
        hashes.push_back(hash);
    }
    if (profiling) {
        const auto s = observation.summarize();
        check(s.frames == 120 && s.interval.samples == 119, "all rendered frames observed");
        check(s.changedNative == (moving ? 120U : 1U) &&
              s.unchangedNative == (moving ? 0U : 119U), "static vs moving native output");
        check(s.inputFrames == 6 && s.noInputUnchanged == (moving ? 0U : 114U), "input != visual invalidation");
        std::printf("FRAME_ACTIVITY_SCENE width=%d height=%d moving=%d rendered=%u changed=%u unchanged=%u input_frames=%u\n",
                    width, height, moving, s.frames, s.changedNative, s.unchangedNative, s.inputFrames);
        observation.clearWindow();
        RendererTestAccess::invalidate(renderer);
        check(RendererTestAccess::submit(renderer, surface.get()), "invalidation requires full refresh");
        check(renderer.presentStats().fullFrame && renderer.presentStats().rects == 1, "full refresh after invalidation");
        acceptFrame = false;
        check(!RendererTestAccess::submit(renderer, surface.get()), "native rejection");
        const auto &rejected = renderer.presentStats();
        check(observation.record(2000000, 2000010, 2001000, 0, rejected.native,
                                 rejected.fallback, rejected.rects), "record rejection");
        check(observation.summarize().unknown == 1 && observation.summarize().unchangedNative == 0,
              "rejected zero-damage callback is not unchanged evidence");
        acceptFrame = true;
        check(RendererTestAccess::submit(renderer, surface.get()), "recover native path");
        check(renderer.presentStats().fullFrame, "recovery requires full refresh");
    }
    return hashes;
}

int main() {
    try {
        for (const auto &size : {std::pair<int,int>{320,240}, {640,480}, {856,480}})
            for (bool moving : {false, true})
                check(scene(false, moving, size.first, size.second) ==
                      scene(true, moving, size.first, size.second), "profile changed rendered output");
        std::printf("FRAME_ACTIVITY_RENDERER_OK checks=%u paired_frames=720 sizes=3\n", checks);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
