#include "Renderer.h"
#include "SettingsView.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace lilsmoke;
static int checks = 0;
static void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
    ++checks;
    std::cout << "PASS " << message << '\n';
}

int main(int argc, char **argv) {
    try {
        const std::filesystem::path out = argc > 1 ? argv[1] : "/tmp/lilsmoke-tv-qa";
        std::filesystem::create_directories(out);
        const auto path = out / "settings.conf";
        {
            std::ofstream f(path);
            f << "DEVICE_MODE=handheld\nTV_SAFE_AREA_PERCENT=5\nSHOW_BATTERY=text\n";
        }
        Config config;
        check(config.load(path.string()), "isolated settings loaded");
        SettingsView settings(config);
        int changed = 0;
        settings.setOnPresentationChanged([&]() { ++changed; });
        settings.onEnter();
        for (int i = 0; i < 5; ++i) settings.handleInput(InputButton::Down, InputEvent::Pressed);
        settings.handleInput(InputButton::Right, InputEvent::Pressed);
        check(config.get("DEVICE_MODE") == "tv" && changed == 1,
              "controller switches to TV mode and invokes runtime callback");
        settings.handleInput(InputButton::Down, InputEvent::Pressed);
        settings.handleInput(InputButton::Right, InputEvent::Pressed);
        check(config.get("TV_SAFE_AREA_PERCENT") == "7" && changed == 2,
              "controller adjusts safe margin and invokes runtime callback");
        for (int i = 0; i < 3; ++i) settings.handleInput(InputButton::Down, InputEvent::Pressed);
        settings.handleInput(InputButton::Left, InputEvent::Pressed);
        check(config.get("SHOW_BATTERY") == "text", "TV battery setting cannot overwrite handheld preference");
        Config reload;
        check(reload.load(path.string()) && reload.get("DEVICE_MODE") == "tv" &&
              reload.get("TV_SAFE_AREA_PERCENT") == "7", "TV settings survive reload");
        if (argc > 1) {
            for (auto dimensions : {std::pair<int,int>{640,480}, {854,480}, {320,240}}) {
                Renderer r;
                check(r.init(dimensions.first, dimensions.second), "software output initialized");
                r.setSafeAreaPercent(5);
                check(r.width() >= 320 && r.height() >= 240, "safe margins retain minimum usable UI size");
                SDL_Rect viewport;
                auto *sdl = static_cast<SDL_Renderer *>(r.sdlRenderer());
                SDL_RenderGetViewport(sdl, &viewport);
                check(viewport.x == r.insetX() && viewport.y == r.insetY() &&
                      viewport.w == r.width() && viewport.h == r.height(), "renderer and UI agree on viewport");
                r.clear(0xCC3344FF);
                SDL_RenderSetViewport(sdl, nullptr);
                SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, dimensions.first,
                    dimensions.second, 32, SDL_PIXELFORMAT_RGBA32);
                check(surface && SDL_RenderReadPixels(sdl, nullptr, surface->format->format,
                      surface->pixels, surface->pitch) == 0, "actual software pixels read back");
                Uint8 red, green, blue;
                auto pixel = [&](int x, int y) {
                    Uint32 value;
                    std::memcpy(&value, static_cast<Uint8 *>(surface->pixels) + y * surface->pitch + x * 4, 4);
                    SDL_GetRGB(value, surface->format, &red, &green, &blue);
                };
                pixel(dimensions.first / 2, dimensions.second / 2);
                check(red == 204 && green == 51 && blue == 68, "content renders inside safe area");
                if (r.insetX() || r.insetY()) {
                    pixel(0, 0);
                    check(red == 0 && green == 0 && blue == 0, "overscan border is black");
                }
                SDL_FreeSurface(surface);
                SDL_RenderSetViewport(sdl, &viewport);
                Font font;
                check(font.load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16), "QA font loaded");
                Theme theme;
                settings.onEnter();
                settings.render(r, font, theme);
                SDL_RenderSetViewport(sdl, nullptr);
                surface = SDL_CreateRGBSurfaceWithFormat(0, dimensions.first, dimensions.second,
                                                         32, SDL_PIXELFORMAT_RGBA32);
                check(surface && SDL_RenderReadPixels(sdl, nullptr, surface->format->format,
                      surface->pixels, surface->pitch) == 0, "settings pixels read back");
                bool gapClear = true;
                for (int y = 135; y < 158; ++y) {
                    for (int x = r.width() / 2 - 22; x < r.width() / 2; ++x) {
                        pixel(x + r.insetX(), y + r.insetY());
                        if (red > 120 && green > 120) gapClear = false;
                    }
                }
                check(gapClear, "long settings label does not overlap the value column");
                const auto png = out / ("tv-settings-" + std::to_string(dimensions.first) + ".png");
                check(IMG_SavePNG(surface, png.c_str()) == 0, "actual settings screenshot saved");
                SDL_FreeSurface(surface);
                r.setSafeAreaPercent(999);
                check(r.insetX() <= dimensions.first / 10, "invalid large margin is bounded");
                r.setSafeAreaPercent(-1);
                check(r.width() == dimensions.first && r.height() == dimensions.second,
                      "negative margin restores full viewport");
            }
        }
        std::cout << "TV_UI_OK checks=" << checks << '\n';
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
