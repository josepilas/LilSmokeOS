#define SDL_MAIN_HANDLED

#include "Config.h"
#include "FileExplorerView.h"
#include "Font.h"
#include "GridView.h"
#include "Input.h"
#include "ListView.h"
#include "Renderer.h"
#include "ROMScanner.h"
#include "SettingsView.h"
#include "Theme.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static bool saveFrame(lilsmoke::Renderer &renderer, const fs::path &path) {
    SDL_Renderer *sdl = static_cast<SDL_Renderer *>(renderer.sdlRenderer());
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, renderer.width(), renderer.height(), 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        std::fprintf(stderr, "SDL_CreateRGBSurfaceWithFormat failed: %s\n", SDL_GetError());
        return false;
    }

    int rc = SDL_RenderReadPixels(
        sdl, nullptr, surface->format->format, surface->pixels, surface->pitch);
    if (rc != 0) {
        std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return false;
    }

    rc = SDL_SaveBMP(surface, path.string().c_str());
    SDL_FreeSurface(surface);
    if (rc != 0) {
        std::fprintf(stderr, "SDL_SaveBMP(%s) failed: %s\n",
                     path.string().c_str(), SDL_GetError());
        return false;
    }
    return true;
}

static int renderFrame(lilsmoke::View &view,
                       lilsmoke::Renderer &renderer,
                       lilsmoke::Font &font,
                       lilsmoke::Theme &theme,
                       const fs::path &path) {
    view.render(renderer, font, theme);
    renderer.present();
    if (!saveFrame(renderer, path)) return 1;
    return 0;
}

static void press(lilsmoke::View &view, lilsmoke::InputButton button) {
    view.handleInput(button, lilsmoke::InputEvent::Pressed);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ui_render_smoke <out-dir> <font-path>\n");
        return 2;
    }

    fs::path outDir = argv[1];
    fs::path fontPath = argv[2];
    fs::create_directories(outDir);

    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    lilsmoke::Renderer renderer;
    if (!renderer.init(640, 480)) return 3;

    lilsmoke::Font font;
    if (!font.load(fontPath.string(), 16)) return 4;

    lilsmoke::Theme theme;

    lilsmoke::ROMScanner scanner;
    int launchables = scanner.scan("/storage/roms");
    auto systems = scanner.systems();
    bool sawPortsmoke = false;
    bool sawSnes = false;
    for (const auto &system : systems) {
        if (system == "portsmoke") sawPortsmoke = true;
        if (system == "snes") sawSnes = true;
    }

    std::vector<std::string> rootItems = {"Settings", "LilSmoke Explorer"};
    for (const auto &system : systems) {
        if (system == "portsmoke") rootItems.push_back("PortSmoke");
        else if (system == "snes") rootItems.push_back("SNES / Super Famicom");
        else rootItems.push_back(system);
    }
    rootItems.push_back("Exit");

    int selectedRoot = -1;
    lilsmoke::ListView root(rootItems,
        [&selectedRoot](int idx, const std::string &) -> lilsmoke::View * {
            selectedRoot = idx;
            return nullptr;
        },
        "LilSmokeOS");
    root.onEnter();
    if (renderFrame(root, renderer, font, theme, outDir / "01-root.bmp") != 0) return 5;
    press(root, lilsmoke::InputButton::Down);
    press(root, lilsmoke::InputButton::A);
    if (selectedRoot != 1) {
        std::fprintf(stderr, "root navigation failed: selectedRoot=%d\n", selectedRoot);
        return 6;
    }
    if (renderFrame(root, renderer, font, theme, outDir / "02-root-explorer-selected.bmp") != 0)
        return 7;

    lilsmoke::Config config;
    lilsmoke::SettingsView settings(config);
    settings.onEnter();
    if (renderFrame(settings, renderer, font, theme, outDir / "03-settings.bmp") != 0)
        return 8;

    const char *explorerRoot = std::getenv("LILSMOKE_UI_SMOKE_STORAGE");
    if (!explorerRoot || explorerRoot[0] == '\0') explorerRoot = "/storage";
    lilsmoke::FileExplorerView explorer(explorerRoot);
    explorer.onEnter();
    if (renderFrame(explorer, renderer, font, theme, outDir / "04-explorer.bmp") != 0)
        return 9;
    press(explorer, lilsmoke::InputButton::X);
    if (renderFrame(explorer, renderer, font, theme, outDir / "05-explorer-menu.bmp") != 0)
        return 10;

    auto ports = scanner.bySystem("portsmoke");
    std::vector<const lilsmoke::ROM *> portPtrs;
    for (const auto *port : ports) portPtrs.push_back(port);
    if (portPtrs.empty()) {
        std::fprintf(stderr, "PortSmoke scan returned no entries\n");
        return 11;
    }
    lilsmoke::GridView portGrid(portPtrs,
        [](const lilsmoke::ROM *) -> lilsmoke::View * { return nullptr; },
        "PortSmoke");
    portGrid.onEnter();
    if (renderFrame(portGrid, renderer, font, theme, outDir / "06-portsmoke-grid.bmp") != 0)
        return 12;

    int snesRootIndex = -1;
    for (int i = 0; i < (int)rootItems.size(); ++i) {
        if (rootItems[i] == "SNES / Super Famicom") {
            snesRootIndex = i;
            break;
        }
    }
    if (snesRootIndex < 0) {
        std::fprintf(stderr, "root menu does not contain SNES / Super Famicom\n");
        return 13;
    }
    selectedRoot = -1;
    root.setSelectedIndex(snesRootIndex);
    if (renderFrame(root, renderer, font, theme, outDir / "07-root-snes-selected.bmp") != 0)
        return 14;
    press(root, lilsmoke::InputButton::A);
    if (selectedRoot != snesRootIndex) {
        std::fprintf(stderr, "SNES root selection failed: selectedRoot=%d expected=%d\n",
                     selectedRoot, snesRootIndex);
        return 15;
    }

    auto snes = scanner.bySystem("snes");
    std::vector<const lilsmoke::ROM *> snesPtrs;
    for (const auto *rom : snes) snesPtrs.push_back(rom);
    if (snesPtrs.empty()) {
        std::fprintf(stderr, "SNES scan returned no entries\n");
        return 16;
    }
    const lilsmoke::ROM *selectedSnes = nullptr;
    lilsmoke::GridView snesGrid(snesPtrs,
        [&selectedSnes](const lilsmoke::ROM *rom) -> lilsmoke::View * {
            selectedSnes = rom;
            return nullptr;
        },
        "SNES / Super Famicom");
    snesGrid.onEnter();
    if (renderFrame(snesGrid, renderer, font, theme, outDir / "08-snes-grid.bmp") != 0)
        return 17;
    press(snesGrid, lilsmoke::InputButton::A);
    if (!selectedSnes) {
        std::fprintf(stderr, "SNES ROM activation failed\n");
        return 18;
    }

    std::printf("UI_RENDER_SMOKE_OK launchables=%d systems=%zu portsmoke=%d snes=%d selected_snes=%s\n",
                launchables, systems.size(), sawPortsmoke ? 1 : 0, sawSnes ? 1 : 0,
                selectedSnes->path.c_str());
    return (sawPortsmoke && sawSnes) ? 0 : 19;
}
