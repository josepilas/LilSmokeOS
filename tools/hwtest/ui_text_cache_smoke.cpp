#define SDL_MAIN_HANDLED
#include "Font.h"
#include "Renderer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace lilsmoke {
struct FontTestAccess {
    static TTF_Font *base(Font &font) { return font.font_; }
    static size_t faces(const Font &font) { return font.cache_.size(); }
};
struct RendererTestAccess {
    static size_t entries(const Renderer &renderer) { return renderer.textCache_.size(); }
    static size_t bytes(const Renderer &renderer) { return renderer.textCacheBytes_; }
};
}
using namespace lilsmoke;
static int checks = 0;
static void check(bool ok, const char *label) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
    ++checks;
}
static std::vector<Uint32> pixels(Renderer &r) {
    int width = 0, height = 0;
    check(SDL_GetRendererOutputSize(static_cast<SDL_Renderer *>(r.sdlRenderer()), &width, &height) == 0,
          "query actual output size");
    std::vector<Uint32> result(static_cast<size_t>(width) * height);
    SDL_Rect rect{0, 0, width, height};
    check(SDL_RenderReadPixels(static_cast<SDL_Renderer *>(r.sdlRenderer()), &rect,
          SDL_PIXELFORMAT_ARGB8888, result.data(), width * 4) == 0, "read pixels");
    return result;
}
static void reference(Renderer &r, TTF_Font *font, const std::string &text,
                      int x, int y, uint32_t color, double rotation = 0.0, bool styled = true) {
    SDL_Color c{static_cast<Uint8>(color >> 24), static_cast<Uint8>(color >> 16),
                static_cast<Uint8>(color >> 8), static_cast<Uint8>(color)};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), c);
    check(surface != nullptr, "reference rasterization");
    SDL_Texture *texture = SDL_CreateTextureFromSurface(
        static_cast<SDL_Renderer *>(r.sdlRenderer()), surface);
    check(texture != nullptr, "reference upload");
    SDL_Rect dst{x, y, surface->w, surface->h};
    SDL_Point center{dst.w / 2, dst.h / 2};
    SDL_FreeSurface(surface);
    if (styled)
        SDL_RenderCopyEx(static_cast<SDL_Renderer *>(r.sdlRenderer()), texture, nullptr,
                         &dst, rotation, &center, SDL_FLIP_NONE);
    else
        SDL_RenderCopy(static_cast<SDL_Renderer *>(r.sdlRenderer()), texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    SDL_SetMainReady();
    Font font;
    {
        Renderer r;
        check(r.init(640, 480), "renderer init");
        check(font.load(argv[1], 24), "font load");
        TTF_Font *base = FontTestAccess::base(font);
        for (uint32_t color : {0xFFFFFFFFU, 0xE1347780U, 0x30BB9000U}) {
            r.clear(0x223344FF);
            reference(r, base, "Native UI: UTF-8 \xc3\xa9", 23, 47, color, 0.0, false);
            const auto expected = pixels(r);
            r.clear(0x223344FF);
            font.render(r, "Native UI: UTF-8 \xc3\xa9", 23, 47, color);
            const auto actual = pixels(r);
            if (actual != expected) {
                size_t differences = 0;
                for (size_t i = 0; i < actual.size(); ++i) {
                    if (actual[i] == expected[i]) continue;
                    if (!differences) std::fprintf(stderr, "pixel mismatch color=%08x x=%zu y=%zu actual=%08x expected=%08x\n",
                        color, i % 640, i / 640, actual[i], expected[i]);
                    ++differences;
                }
                std::fprintf(stderr, "different pixels=%zu\n", differences);
            }
            check(actual == expected, "cached basic pixels equal uncached RGBA reference");
            const size_t entries = RendererTestAccess::entries(r);
            r.clear(0x223344FF);
            font.render(r, "Native UI: UTF-8 \xc3\xa9", 23, 47, color);
            check(pixels(r) == expected && RendererTestAccess::entries(r) == entries,
                  "cache hit keeps pixels and entry count");
        }
        for (double angle : {0.0, 17.0}) {
            const int lineHeight = TTF_FontLineSkip(base);
            const int advance = static_cast<int>(lineHeight * 1.5f);
            r.clear(0x223344FF);
            int w = 0, h = 0;
            TTF_SizeUTF8(base, "alpha", &w, &h);
            reference(r, base, "alpha", 20 + (200 - w) / 2, 40 + (180 - lineHeight - advance) / 2,
                      0x77CCBB90, angle);
            TTF_SizeUTF8(base, "beta", &w, &h);
            reference(r, base, "beta", 20 + (200 - w) / 2,
                      40 + (180 - lineHeight - advance) / 2 + advance, 0x77CCBB90, angle);
            const auto expected = pixels(r);
            r.clear(0x223344FF);
            font.renderStyled(r, "alpha\nbeta", 20, 40, 200, 180, 0x77CCBB90,
                              "", 24, "center", "center", false, 1.5f, angle);
            check(pixels(r) == expected, "styled multiline alignment spacing rotation unchanged");
        }
        r.setClipRect(40, 50, 65, 30);
        r.clear(0x223344FF);
        reference(r, base, "clipped", 20, 47, 0xFFFFFFFF, 0.0, false);
        const auto clipped = pixels(r);
        r.clear(0x223344FF);
        font.render(r, "clipped", 20, 47, 0xFFFFFFFF);
        check(pixels(r) == clipped, "cached clipping unchanged");
        r.clearClipRect();
        for (int i = 0; i < 350; ++i) {
            font.render(r, "bounded text " + std::to_string(i), 0, 0, 0xFFFFFFFF);
            check(RendererTestAccess::entries(r) <= 128 && RendererTestAccess::bytes(r) <= 1024U * 1024U,
                  "text entries and pixels bounded under churn");
        }
        for (int size = 6; size <= 160; ++size) {
            int w, h;
            font.textSizeStyled("font churn", "", size, w, h);
            check(FontTestAccess::faces(font) <= 16 && w > 0 && h > 0, "font handles bounded under size churn");
        }
        const size_t entries = RendererTestAccess::entries(r), bytes = RendererTestAccess::bytes(r);
        font.render(r, std::string(5000, 'W'), 0, 0, 0xFFFFFFFF);
        check(RendererTestAccess::entries(r) == entries && RendererTestAccess::bytes(r) == bytes,
              "oversized run drawn without retention or cache eviction");
        check(font.load(argv[1], 32), "font reload");
        r.clear(0x223344FF);
        reference(r, FontTestAccess::base(font), "reload", 10, 10, 0xFFFFFFFF, 0.0, false);
        const auto reloaded = pixels(r);
        r.clear(0x223344FF);
        font.render(r, "reload", 10, 10, 0xFFFFFFFF);
        check(pixels(r) == reloaded, "reload cannot reuse stale face textures");
        r.present();
    }
    {
        Renderer r;
        check(r.init(640, 480), "replacement renderer");
        r.clear(0x223344FF);
        font.render(r, "reload", 10, 10, 0xFFFFFFFF);
        check(RendererTestAccess::entries(r) == 1, "renderer owns text lifetime across renderer replacement");
        r.present();
    }
    std::printf("UI_TEXT_CACHE_SMOKE_OK checks=%d\n", checks);
}
