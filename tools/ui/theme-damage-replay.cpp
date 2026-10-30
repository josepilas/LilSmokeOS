#define SDL_MAIN_HANDLED
#include "Renderer.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace lilsmoke;
static int accept(const void *, unsigned, unsigned, unsigned, const int32_t *, unsigned) { return 1; }
namespace lilsmoke {
struct RendererTestAccess {
    static bool present(Renderer &r, SDL_Surface *s) {
        r.nativePresentDamage_ = accept;
        r.profilePresent_ = true;
        r.presentStats_ = {};
        return r.presentNativeSurface(s);
    }
    static const int32_t *rects(const Renderer &r) { return r.damageRects_.data(); }
};
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *f = std::fopen(argv[1], "rb");
    uint32_t dimensions[3];
    if (!f || std::fread(dimensions, sizeof(dimensions), 1, f) != 1) return 3;
    const unsigned w = dimensions[0], h = dimensions[1], frames = dimensions[2];
    if (!w || !h || w > 1920 || h > 1080 || frames > 1000) return 4;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return 5;
    std::vector<uint32_t> front(static_cast<size_t>(w) * h);
    Renderer r;
    std::puts("frame,compare_ms,total_ms,rects,native_bytes");
    for (unsigned i = 0; i < frames; ++i) {
        for (unsigned y = 0; y < h; ++y)
            if (std::fread(static_cast<uint8_t *>(s->pixels) + y * s->pitch, w * 4, 1, f) != 1) return 6;
        const auto start = SDL_GetPerformanceCounter();
        if (!RendererTestAccess::present(r, s)) return 7;
        const auto end = SDL_GetPerformanceCounter();
        const auto &stats = r.presentStats();
        std::printf("%u,%.6f,%.6f,%u,%llu\n", i,
            stats.compareTicks * 1000.0 / SDL_GetPerformanceFrequency(),
            (end - start) * 1000.0 / SDL_GetPerformanceFrequency(), stats.rects,
            static_cast<unsigned long long>(stats.nativeBytes));
        const auto *rects = RendererTestAccess::rects(r);
        for (unsigned n = 0; n < stats.rects; ++n) {
            const auto *rect = rects + n * 4;
            for (int y = rect[1]; y < rect[1] + rect[3]; ++y)
                std::memcpy(front.data() + static_cast<size_t>(y) * w + rect[0],
                    static_cast<uint8_t *>(s->pixels) + y * s->pitch + rect[0] * 4, rect[2] * 4);
        }
        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
            uint32_t p;
            std::memcpy(&p, static_cast<uint8_t *>(s->pixels) + y * s->pitch + x * 4, 4);
            if ((front[static_cast<size_t>(y) * w + x] ^ p) & 0xFFFFFFU) return 8;
        }
    }
    SDL_FreeSurface(s); std::fclose(f);
    std::fprintf(stderr, "DAMAGE_REPLAY_OK frames=%u all_pixels_verified=1\n", frames);
    return 0;
}
