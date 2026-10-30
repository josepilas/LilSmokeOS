#define SDL_MAIN_HANDLED
#include "FileExplorerView.h"
#include "Font.h"
#include "Renderer.h"
#include "Theme.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace lilsmoke;
namespace fs = std::filesystem;
static int checks = 0;
static void check(bool condition, const char *label) {
    if (!condition) throw std::runtime_error(label);
    ++checks; std::cout << "PASS " << label << "\n";
}
static std::string read(const fs::path &path) {
    std::ifstream input(path);
    std::ostringstream text; text << input.rdbuf(); return text.str();
}
static void press(FileExplorerView &view, InputButton button) {
    view.handleInput(button, InputEvent::Pressed);
}
static void menu(FileExplorerView &view, int index) {
    press(view, InputButton::X);
    for (int i = 0; i < index; ++i) press(view, InputButton::Down);
    press(view, InputButton::A);
}
static void snapshot(Renderer &renderer, const fs::path &path) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_RGBA32);
    check(surface != nullptr, "capture allocated");
    check(SDL_RenderReadPixels(static_cast<SDL_Renderer *>(renderer.sdlRenderer()), nullptr,
                               surface->format->format, surface->pixels, surface->pitch) == 0,
          "software framebuffer captured");
    check(IMG_SavePNG(surface, path.c_str()) == 0, "PNG saved");
    SDL_FreeSurface(surface);
}
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    try {
        fs::path fixture = argv[1], persist = argv[2], out = argv[3];
        fs::create_directories(out);
        SDL_SetMainReady(); SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        setenv("LILSMOKE_UI_WINDOWED", "1", 1); setenv("LILSMOKE_UI_HIDDEN", "1", 1);
        Renderer renderer; check(renderer.init(640, 480), "UI software renderer starts");
        Font font; check(font.load("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16), "UI font loads");
        Theme theme;
        FileExplorerView explorer(fixture.string());
        int applied = 0;
        explorer.setOnPackageApplied([&]() { ++applied; });
        explorer.onEnter();
        explorer.render(renderer, font, theme);
        snapshot(renderer, out / "01-package-list.png");
        const auto conf = persist / "config/lilsmoke.conf";
        const auto original = read(conf);
        press(explorer, InputButton::A);
        check(explorer.cursorAt(300, 200) == CursorKind::Text && read(conf) == original && applied == 0,
              "open validates quoted package filename without applying");
        explorer.render(renderer, font, theme);
        snapshot(renderer, out / "02-validation.png");
        press(explorer, InputButton::B);
        menu(explorer, 9);
        check(fs::is_directory(persist / "syspck/packages") &&
              !fs::is_empty(persist / "syspck/packages") && read(conf) == original,
              "Explorer stages through native validator without changing settings");
        press(explorer, InputButton::B);
        menu(explorer, 10);
        check(explorer.cursorAt(300, 200) == CursorKind::Help, "apply opens confirmation");
        explorer.handleInput(InputButton::A, InputEvent::Repeat);
        explorer.handleInput(InputButton::Start, InputEvent::Repeat);
        check(explorer.cursorAt(300, 200) == CursorKind::Help && read(conf) == original,
              "held confirm cannot apply package settings");
        explorer.render(renderer, font, theme);
        snapshot(renderer, out / "03-confirmation.png");
        PointerEvent blank; blank.type = PointerEventType::Pressed; blank.button = PointerButton::Primary;
        blank.x = 630; blank.y = 470; explorer.handlePointer(blank);
        check(explorer.cursorAt(300, 200) == CursorKind::Help && read(conf) == original,
              "backdrop click cannot apply");
        press(explorer, InputButton::B);
        check(read(conf) == original && applied == 0, "cancel preserves configuration");
        menu(explorer, 10);
        press(explorer, InputButton::A);
        check(applied == 1 && read(conf).find("SCALING_MODE=aspect") != std::string::npos &&
              read(persist / "syspck/previous.conf") == original,
              "confirmed apply commits profile and triggers live-config reload callback");
        explorer.render(renderer, font, theme);
        snapshot(renderer, out / "04-applied.png");
        press(explorer, InputButton::B);
        press(explorer, InputButton::Down);
        menu(explorer, 10);
        press(explorer, InputButton::A);
        check(applied == 1 && read(conf).find("SCALING_MODE=aspect") != std::string::npos,
              "invalid package cannot apply or trigger success callback");
        explorer.render(renderer, font, theme);
        snapshot(renderer, out / "05-rejected.png");
        std::cout << "SYSPCK_UI_SMOKE_OK checks=" << checks << "\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << "\n"; return 1;
    }
}
