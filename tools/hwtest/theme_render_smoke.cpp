#define SDL_MAIN_HANDLED

#include "Font.h"
#include "GridView.h"
#include "Input.h"
#include "Renderer.h"
#include "ROMScanner.h"
#include "SystemView.h"
#include "Theme.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static bool saveFrame(lilsmoke::Renderer &renderer, const fs::path &path) {
    SDL_Renderer *sdl = static_cast<SDL_Renderer *>(renderer.sdlRenderer());
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, renderer.width(), renderer.height(), 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return false;
    int rc = SDL_RenderReadPixels(
        sdl, nullptr, surface->format->format, surface->pixels, surface->pitch);
    if (rc == 0) rc = IMG_SavePNG(surface, path.string().c_str());
    SDL_FreeSurface(surface);
    return rc == 0;
}

static bool createPreview(const fs::path &path) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, 480, 270, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return false;
    SDL_FillRect(surface, nullptr, SDL_MapRGB(surface->format, 40, 75, 110));
    SDL_Rect sky = {0, 0, 480, 150};
    SDL_FillRect(surface, &sky, SDL_MapRGB(surface->format, 78, 145, 220));
    SDL_Rect ground = {0, 150, 480, 120};
    SDL_FillRect(surface, &ground, SDL_MapRGB(surface->format, 58, 150, 70));
    for (int i = 0; i < 8; ++i) {
        SDL_Rect block = {24 + i * 58, 112 - (i % 3) * 12, 42, 42};
        SDL_FillRect(surface, &block,
                     SDL_MapRGB(surface->format, 224, 170, 48));
    }
    SDL_Rect player = {220, 128, 28, 44};
    SDL_FillRect(surface, &player, SDL_MapRGB(surface->format, 210, 35, 35));
    int rc = IMG_SavePNG(surface, path.string().c_str());
    SDL_FreeSurface(surface);
    return rc == 0;
}

static bool render(lilsmoke::View &view,
                   lilsmoke::Renderer &renderer,
                   lilsmoke::Font &font,
                   lilsmoke::Theme &theme,
                   const fs::path &path) {
    view.render(renderer, font, theme);
    renderer.present();
    return saveFrame(renderer, path);
}

static void press(lilsmoke::View &view, lilsmoke::InputButton button) {
    view.handleInput(button, lilsmoke::InputEvent::Pressed);
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    fs::path themeDir = argv[1];
    fs::path outputDir = argv[2];
    fs::path fontPath = argv[3];
    fs::create_directories(outputDir);

    setenv("LILSMOKE_SCREEN_WIDTH", "854", 1);
    setenv("LILSMOKE_SCREEN_HEIGHT", "480", 1);
    setenv("LILSMOKE_SHOW_HELP_PROMPTS", "1", 1);
    setenv("LILSMOKE_DRAW_CLOCK", "0", 1);
    setenv("LILSMOKE_SHOW_BATTERY", "text", 1);
    setenv("LILSMOKE_BATTERY_PERCENT", "100", 1);
    setenv("LILSMOKE_UI_WINDOWED", "1", 1);
    setenv("LILSMOKE_UI_HIDDEN", "1", 1);
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    lilsmoke::Renderer renderer;
    renderer.setTextureCacheLimitMB(8);
    if (!renderer.init(854, 480)) return 3;
    lilsmoke::Font font;
    if (!font.load(fontPath.string(), 16)) return 4;

    fs::path preview = outputDir / "preview.png";
    if (!createPreview(preview)) return 5;

    std::string themeSetDir = themeDir.string();
    std::vector<lilsmoke::SystemEntry> systems = {
        {"snes", "Super Nintendo", 77},
        {"nes", "Nintendo Entertainment System", 65},
        {"gba", "Game Boy Advance", 110},
        {"arcade", "Arcade", 1941},
        {"n64", "Nintendo 64", 22},
    };
    lilsmoke::SystemView systemView(
        systems,
        [](const lilsmoke::SystemEntry &) -> lilsmoke::View * { return nullptr; },
        &themeSetDir);
    systemView.setSelectedIndex(0);
    lilsmoke::Theme emptyTheme;
    if (!render(systemView, renderer, font, emptyTheme,
                outputDir / "01-system-snes.png")) return 6;
    press(systemView, lilsmoke::InputButton::Right);
    if (!render(systemView, renderer, font, emptyTheme,
                outputDir / "02-system-next.png")) return 7;

    lilsmoke::Theme snesTheme;
    if (!snesTheme.loadSet(themeSetDir, "snes", "Super Nintendo")) return 8;
    std::vector<lilsmoke::ROM> roms;
    const char *names[] = {
        "Super Mario World (PT-BR)", "Chrono Trigger", "Donkey Kong Country",
        "F-Zero", "Mega Man X", "Super Metroid", "Yoshi's Island",
        "The Legend of Zelda"
    };
    for (int index = 0; index < 8; ++index) {
        lilsmoke::ROM rom;
        rom.system = "snes";
        rom.name = names[index];
        rom.path = "/storage/roms/snes/test.sfc";
        rom.coverPath = preview.string();
        rom.description =
            "A classic 16-bit adventure shown through LilSmokeUI's "
            "EmulationStation-compatible detailed game view.";
        rom.releaseDate = "1990-11-21";
        rom.developer = "Nintendo";
        rom.publisher = "Nintendo";
        rom.genre = "Platform";
        rom.players = "1-2";
        rom.rating = "0.9";
        roms.push_back(std::move(rom));
    }
    std::vector<const lilsmoke::ROM *> pointers;
    for (const auto &rom : roms) pointers.push_back(&rom);
    lilsmoke::GridView games(
        pointers,
        [](const lilsmoke::ROM *) -> lilsmoke::View * { return nullptr; },
        "Super Nintendo", std::move(snesTheme));
    games.onEnter();
    if (!render(games, renderer, font, emptyTheme,
                outputDir / "03-games-first.png")) return 9;
    press(games, lilsmoke::InputButton::Down);
    if (!render(games, renderer, font, emptyTheme,
                outputDir / "04-games-down.png")) return 10;
    press(games, lilsmoke::InputButton::Down);
    if (!render(games, renderer, font, emptyTheme,
                outputDir / "05-games-scroll.png")) return 11;
    games.setSelectedIndex(0);
    if (!render(games, renderer, font, emptyTheme,
                outputDir / "06-games-final.png")) return 12;
    fs::remove(preview);
    return 0;
}
