#define SDL_MAIN_HANDLED
#include "GridView.h"
#include "VirtualKeyboard.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

using namespace lilsmoke;
static void capture(Renderer &renderer, const std::string &path) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, renderer.width(), renderer.height(),
                                                         32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    assert(!SDL_RenderReadPixels(static_cast<SDL_Renderer *>(renderer.sdlRenderer()), nullptr,
                                 surface->format->format, surface->pixels, surface->pitch));
    assert(!IMG_SavePNG(surface, path.c_str()));
    SDL_FreeSurface(surface);
}
int main(int argc, char **argv) {
    assert(argc == 4);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("LILSMOKE_UI_WINDOWED", "1", 1);
    setenv("LILSMOKE_UI_HIDDEN", "1", 1);
    std::filesystem::create_directories(argv[3]);
    Renderer renderer;
    assert(renderer.init(856, 480));
    Font font; assert(font.load(argv[2], 16));
    Theme theme; assert(theme.load(argv[1]));
    const std::vector<std::string> names = {"Chrono Trigger", "Donkey Kong Country", "EarthBound",
        "F-Zero", "Final Fantasy III", "Kirby Super Star", "Mega Man X", "Secret of Mana",
        "Star Fox", "Super Mario All-Stars", "Super Mario Kart", "Super Mario World",
        "Super Metroid", "The Legend of Zelda - A Link to the Past", "Yoshi's Island"};
    std::vector<ROM> roms(names.size());
    std::vector<const ROM *> pointers;
    for (size_t i = 0; i < names.size(); ++i) {
        roms[i].name = names[i]; pointers.push_back(&roms[i]);
    }
    GridView games(std::move(pointers), {}, "SNES", theme);
    games.render(renderer, font, theme);
    capture(renderer, std::string(argv[3]) + "/game-list-856x480.png");
    games.handleInput(InputButton::Y, InputEvent::Pressed);
    games.handleText("Mario");
    VirtualKeyboard keyboard; keyboard.reset();
    keyboard.render(renderer, font, games.textTitle(), games.textValue());
    capture(renderer, std::string(argv[3]) + "/game-search-keyboard-856x480.png");
    games.handleInput(InputButton::Start, InputEvent::Pressed);
    games.render(renderer, font, theme);
    capture(renderer, std::string(argv[3]) + "/game-search-results-856x480.png");
}
