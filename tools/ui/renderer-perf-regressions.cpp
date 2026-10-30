#define SDL_MAIN_HANDLED
#include "Renderer.h"
#include "Font.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

using namespace lilsmoke;
namespace fs = std::filesystem;
static unsigned checks;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
namespace lilsmoke {
struct RendererTestAccess {
    static bool headless(Renderer &r, SDL_Surface *s) {
        r.width_ = r.outputWidth_ = s->w;
        r.height_ = r.outputHeight_ = s->h;
        r.renderer_ = SDL_CreateSoftwareRenderer(s);
        return r.renderer_ != nullptr;
    }
    static size_t images(const Renderer &r) { return r.textureCache_.size(); }
    static size_t imageBytes(const Renderer &r) { return r.textureCacheBytes_; }
    static size_t textBytes(const Renderer &r) { return r.textCacheBytes_; }
    static size_t aliases(const Renderer &r) { return r.imageAliases_.size(); }
};
struct FontTestAccess {
    static size_t bytes(const Font &f) { return f.layoutBytes_; }
    static size_t count(const Font &f) { return f.layouts_.size(); }
};
}
static std::vector<uint32_t> pixels(Renderer &r) {
    std::vector<uint32_t> result(static_cast<size_t>(r.width()) * r.height());
    check(SDL_RenderReadPixels(static_cast<SDL_Renderer *>(r.sdlRenderer()), nullptr,
        SDL_PIXELFORMAT_ARGB8888, result.data(), r.width() * 4) == 0, "read pixels");
    return result;
}
static void tileFixture(const fs::path &path, bool pattern, bool transparent) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 3, 2, 32, SDL_PIXELFORMAT_RGBA8888);
    check(s != nullptr, "fixture surface");
    SDL_FillRect(s, nullptr, 0x68AC34FF);
    if (transparent) SDL_FillRect(s, nullptr, 0x68AC347F);
    if (pattern) { SDL_Rect cell{1, 0, 1, 1}; SDL_FillRect(s, &cell, 0x773399FF); }
    check(IMG_SavePNG(s, path.c_str()) == 0, "fixture PNG"); SDL_FreeSurface(s);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "output directory required"); fs::path out = argv[1]; fs::create_directories(out / "sub");
        setenv("SDL_VIDEODRIVER", "dummy", 1); setenv("LILSMOKE_UI_WINDOWED", "1", 1);
        setenv("LILSMOKE_UI_NATIVE_DAMAGE", "0", 1); SDL_SetMainReady();
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(nullptr, SDL_FreeSurface);
        Renderer r;
        if (std::getenv("QA_HEADLESS")) {
            check(SDL_Init(0) == 0, "SDL timer-only initialization");
            surface.reset(SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_ARGB8888));
            check(surface && RendererTestAccess::headless(r, surface.get()), "headless software renderer");
        } else check(r.init(320, 240), "renderer");
        auto *sdl = static_cast<SDL_Renderer *>(r.sdlRenderer());
        for (const auto name : {"solid", "pattern", "alpha"}) {
            auto path = out / (std::string(name) + ".png");
            tileFixture(path, std::string(name) == "pattern", std::string(name) == "alpha");
            auto *texture = r.loadImage(path.string()); check(texture != nullptr, "fixture texture");
            for (int alpha : {255, 127, 0}) for (int modulation : {255, 153})
                for (bool clipped : {false, true}) for (bool offscreen : {false, true}) {
                    SDL_SetTextureAlphaMod(texture, alpha);
                    SDL_SetTextureColorMod(texture, modulation, 87, 231);
                    const int x = offscreen ? -3 : 5, y = offscreen ? -2 : 7, w = 47, h = 31;
                    r.clear(0x2371B3FF);
                    if (clipped) r.setClipRect(11, 13, 27, 16); else r.clearClipRect();
                    r.drawTextureTiled(texture, x, y, w, h);
                    const auto actual = pixels(r);
                    r.clear(0x2371B3FF);
                    for (int yy = 0; yy < h; yy += 2) for (int xx = 0; xx < w; xx += 3) {
                        SDL_Rect src{0, 0, std::min(3, w-xx), std::min(2, h-yy)};
                        SDL_Rect dst{x+xx, y+yy, src.w, src.h};
                        SDL_RenderCopy(sdl, texture, &src, &dst);
                    }
                    check(actual == pixels(r), "tile replacement must be pixel-identical");
                }
        }
        r.clearClipRect();
        const auto solidPath = out / "solid.png";
        auto *solid = r.loadImage(solidPath.string());
        check(r.loadImage((out / "sub/../solid.png").string()) == solid, "include aliases share decoded texture");
        r.invalidateImage((out / "sub/.././solid.png").string());
        check(RendererTestAccess::images(r) == 2, "uncached alias invalidates canonical texture");
        check(r.loadImage(solidPath.string()) != nullptr, "invalidated artwork reloads");
        Font f; check(f.load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16), "font");
        for (int i = 0; i < 500; ++i) {
            std::string text = "Word wrapping and cache eviction test " + std::to_string(i);
            int w, h; f.textSizeStyled(text, "", 16, w, h);
            f.renderStyled(r, text, 0, 0, 140 + i % 2, 120, 0x68AC347F, "", 16, "center", "center", true);
            check(FontTestAccess::bytes(f) <= 64U * 1024U && FontTestAccess::count(f) <= 128, "bounded font layout cache");
            check(RendererTestAccess::textBytes(r) + FontTestAccess::bytes(f) <= 1024U * 1024U, "combined text caches under one MiB");
        }
        std::string aliasPath = out.string() + "/";
        for (int i = 0; i < 300; ++i) {
            aliasPath += "sub/../";
            check(r.loadImage(aliasPath + "solid.png") != nullptr, "bounded alias stress");
        }
        check(RendererTestAccess::aliases(r) <= 256 && RendererTestAccess::imageBytes(r) <= 8U * 1024U * 1024U, "bounded image cache");
        std::printf("RENDERER_PERF_REGRESSIONS_OK checks=%u\n", checks);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL %s\n", e.what()); return 1; }
}
