#define SDL_MAIN_HANDLED
#include "Font.h"
#include "Renderer.h"
#include "ThemeRender.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lilsmoke;
namespace lilsmoke {
struct RendererTestAccess {
    static bool init(Renderer &r, SDL_Surface *surface) {
        r.width_ = r.outputWidth_ = surface->w;
        r.height_ = r.outputHeight_ = surface->h;
        r.softwareSurface_ = surface;
        r.renderer_ = SDL_CreateSoftwareRenderer(surface);
        return r.renderer_ != nullptr;
    }
};
}

static unsigned checks;
static void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
    ++checks;
}
static std::string pair(const std::array<float, 2> &p) {
    return std::to_string(p[0]) + " " + std::to_string(p[1]);
}
static ThemeElement legacy(const ThemeElement &element, const themeui::ElementLayout &layout) {
    auto adjusted = element;
    if (layout.ignoreSize) adjusted.props.erase("size");
    if (layout.ignoreColor) adjusted.props.erase("color");
    if (layout.pos) adjusted.props["pos"] = pair(*layout.pos);
    if (layout.origin) adjusted.props["origin"] = pair(*layout.origin);
    if (layout.size) adjusted.props["size"] = pair(*layout.size);
    if (layout.maxSize) adjusted.props["maxsize"] = pair(*layout.maxSize);
    if (layout.rotationOrigin) adjusted.props["rotationorigin"] = pair(*layout.rotationOrigin);
    if (layout.rotation) adjusted.props["rotation"] = std::to_string(*layout.rotation);
    if (layout.fontSize) adjusted.props["fontsize"] = std::to_string(*layout.fontSize);
    return adjusted;
}
static std::vector<uint32_t> pixels(Renderer &r) {
    std::vector<uint32_t> out(static_cast<size_t>(r.width()) * r.height());
    check(SDL_RenderReadPixels(static_cast<SDL_Renderer *>(r.sdlRenderer()), nullptr,
        SDL_PIXELFORMAT_ARGB8888, out.data(), r.width() * 4) == 0, "read pixels");
    return out;
}
static void equivalent(Renderer &r, Font &font, const ThemeElement &element,
                       const themeui::ElementLayout &layout, bool text,
                       uint32_t time, const char *event) {
    const auto beforeProps = element.props;
    const auto adjusted = legacy(element, layout);
    themeui::PaintedRegion a, b;
    themeui::MotionScope motion(time, 3, -2, 173, event);
    r.clear(0x26486BFF);
    r.setClipRect(5, 7, r.width() - 15, r.height() - 19);
    if (text) themeui::drawText(r, font, adjusted, "MIPS Layout", 0xF0D090AD, 0, 0, 1, 1, &a);
    else themeui::drawImage(r, adjusted, "", 0, 0, 1, 1, 191, &a);
    const auto old = pixels(r);
    r.clearClipRect(); r.clear(0x26486BFF);
    r.setClipRect(5, 7, r.width() - 15, r.height() - 19);
    if (text) themeui::drawText(r, font, element, "MIPS Layout", 0xF0D090AD, 0, 0, 1, 1, &b, &layout);
    else themeui::drawImage(r, element, "", 0, 0, 1, 1, 191, &b, &layout);
    check(old == pixels(r), "typed layout changed reference pixels");
    check(a.rect.x == b.rect.x && a.rect.y == b.rect.y && a.rect.w == b.rect.w && a.rect.h == b.rect.h,
        "typed layout changed painted bounds");
    for (int y = -1; y <= r.height(); y += 7) for (int x = -1; x <= r.width(); x += 7)
        check(a.contains(x, y) == b.contains(x, y), "typed layout changed hit region");
    check(element.props == beforeProps, "draw mutated source properties");
    r.clearClipRect();
}

int main(int argc, char **argv) {
    try {
        check(argc == 3, "fixture directory and font required");
        SDL_SetMainReady(); check(SDL_Init(0) == 0, "SDL init");
        std::filesystem::create_directories(argv[1]);
        const auto icon = std::filesystem::path(argv[1]) / "layout-fixture.png";
        auto *image = SDL_CreateRGBSurfaceWithFormat(0, 23, 17, 32, SDL_PIXELFORMAT_ARGB8888);
        check(image != nullptr, "fixture surface");
        for (int y = 0; y < image->h; ++y) for (int x = 0; x < image->w; ++x) {
            const uint32_t p = SDL_MapRGBA(image->format, (x*13+y*5)%256, (y*17)%256,
                                           (x*7+y*11)%256, (x*11+y*19)%256);
            std::memcpy(static_cast<uint8_t *>(image->pixels) + y*image->pitch + x*4, &p, 4);
        }
        check(IMG_SavePNG(image, icon.c_str()) == 0, "fixture PNG"); SDL_FreeSurface(image);
        for (const auto size : {std::array<int, 2>{320, 240}, {856, 480}, {1280, 720}}) {
            auto *surface = SDL_CreateRGBSurfaceWithFormat(0, size[0], size[1], 32, SDL_PIXELFORMAT_ARGB8888);
            check(surface != nullptr, "target surface");
            {
                Renderer r; check(RendererTestAccess::init(r, surface), "software renderer");
                Font font; check(font.load(argv[2], 16), "font");
                ThemeElement element;
                element.type = "image"; element.name = "logo";
                element.props = {{"path", icon.string()}, {"pos", "0.2 0.3"}, {"size", "0.9 0.8"},
                    {"origin", "0 0"}, {"rotation", "-5"}, {"color", "d090e0c0"},
                    {"fontpath", argv[2]}, {"alignment", "center"}, {"fontsize", "0.03125"}};
                for (bool text : {false, true}) for (int variant = 0; variant < 8; ++variant) {
                    themeui::ElementLayout layout;
                    layout.pos = {{0.25f + variant / 16.0f, 0.5f}};
                    layout.origin = {{variant % 2 ? 0.0f : 0.5f, 0.5f}};
                    if (text) { layout.size = {{0.5f, 0.25f}}; layout.fontSize = 0.03125f; layout.ignoreColor = true; }
                    else { layout.ignoreSize = true; layout.maxSize = {{0.5f, 0.25f}}; }
                    layout.rotation = variant % 2 ? -7.5f : 0.0f;
                    layout.rotationOrigin = {{-0.5f, 0.5f}};
                    equivalent(r, font, element, layout, text, 0, "activate");
                    using A = ThemeAnimation;
                    A rotate, move;
                    rotate.property = A::Property::Rotation; rotate.to = 50; rotate.durationMs = 160;
                    rotate.mode = A::Mode::EaseInOut; rotate.hasTo = true; rotate.autoReverse = true; rotate.repeat = 3;
                    move.property = A::Property::X; move.from = 0.25f; move.to = 0.75f;
                    move.durationMs = 160; move.hasFrom = move.hasTo = true;
                    element.animations = {rotate, move};
                    element.animations[0].beginMs = 32;
                    element.animations[0].event = "activate";
                    element.animations[0].storyboardRepeat = 2;
                    element.animations[0].storyboardDurationMs = 400;
                    for (uint32_t time : {0u, 31u, 32u, 111u, 192u, 320u, 399u, 400u, 799u, 1200u})
                        for (const char *event : {"activate", "deactivate"})
                            equivalent(r, font, element, layout, text, time, event);
                    element.animations.clear();
                }
            }
            SDL_FreeSurface(surface);
        }
        themeui::PaintedRegion p{{3, 4, 10, 8}, {0, 0, 100, 100}};
        check(p.contains(3, 4) && p.contains(12, 11) && !p.contains(13, 11) && !p.contains(12, 12),
              "zero rotation integer hit edges");
        std::printf("CANDIDATE42_LAYOUT_OK checks=%u sizes=3\n", checks);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
