// SPDX-License-Identifier: MPL-2.0
#include "TreeFrogPortsView.h"
#include "ProcessLaunch.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <unistd.h>

using namespace lilsmoke;
namespace fs = std::filesystem;
static bool helperPresent = true;
extern "C" int __real_access(const char *, int);
extern "C" int __wrap_access(const char *path, int mode) {
    if (!std::strcmp(path, "/usr/lib/lilsmoke/lilsmoke-treefrog-launch")) return helperPresent ? 0 : -1;
    return __real_access(path, mode);
}
static void write(const fs::path &path, const std::string &value) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path); out << value; assert(out.good());
}
static void snapshot(View &view, Renderer &renderer, Font &font, Theme &theme, const fs::path &path) {
    view.render(renderer, font, theme);
    auto surface = SDL_CreateRGBSurfaceWithFormat(0, renderer.width(), renderer.height(), 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    auto sdl = static_cast<SDL_Renderer *>(renderer.sdlRenderer());
    assert(!SDL_RenderReadPixels(sdl, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch));
    const auto pixels = static_cast<uint32_t *>(surface->pixels);
    bool varied = false;
    for (int i = 1; i < renderer.width() * renderer.height(); ++i) varied |= pixels[i] != pixels[0];
    assert(varied);
    assert(!SDL_SaveBMP(surface, path.c_str()));
    SDL_FreeSurface(surface);
}
int main(int argc, char **argv) {
    assert(argc == 4);
    char temp[] = "/tmp/lilsmoke-treefrog-ui-XXXXXX";
    assert(mkdtemp(temp));
    const fs::path root = temp;
    struct Cleanup { fs::path path; ~Cleanup() { fs::remove_all(path); } } cleanup{root};
    const auto folder = root / "sample-native";
    std::ifstream fixture(fs::path(argv[1]) / "native.json");
    assert(fixture);
    const std::string native{std::istreambuf_iterator<char>(fixture), {}};
    write(folder / "config.json", native);
    write(folder / "run.sh", "exit 99\n");
    write(folder / "bin/game", "fixture");
    Renderer renderer;
    const int width = std::getenv("QA_SMALL_SCREEN") ? 320 : 640;
    assert(renderer.initOffscreen(width, width * 3 / 4));
    Font font;
    assert(font.load(argv[2], 16));
    Theme theme;
    TreeFrogPortsView view(root);
    view.onEnter();
    snapshot(view, renderer, font, theme, fs::path(argv[3]) / ("ports-" + std::to_string(width) + ".bmp"));
    assert(!view.handleInput(InputButton::A, InputEvent::Repeat) && !processLaunchRequested());
    assert(!view.handleInput(InputButton::A, InputEvent::Pressed));
    auto launch = takeProcessLaunch();
    assert(launch && launch->executable == "/usr/lib/lilsmoke/lilsmoke-treefrog-launch");
    assert(launch->arguments == std::vector<std::string>({"lilsmoke-treefrog-launch", "sample-native"}));
    helperPresent = false;
    std::unique_ptr<View> notice(view.handleInput(InputButton::Start, InputEvent::Pressed));
    assert(notice && !processLaunchRequested());
    notice.reset();
    helperPresent = true;
    fs::remove(folder / "bin/game");
    notice.reset(view.handleInput(InputButton::A, InputEvent::Pressed));
    assert(notice && !processLaunchRequested()); // Selection revalidates without re-entry.
    snapshot(*notice, renderer, font, theme, fs::path(argv[3]) / ("missing-" + std::to_string(width) + ".bmp"));
    view.onEnter();
    notice.reset(view.handleInput(InputButton::A, InputEvent::Pressed));
    assert(notice && !processLaunchRequested());
    write(folder / "bin/game", "fixture");
    view.onEnter();
    assert(!view.handleInput(InputButton::A, InputEvent::Pressed) && takeProcessLaunch());
    write(folder / "config.json", "{}");
    notice.reset(view.handleInput(InputButton::A, InputEvent::Pressed));
    assert(notice && !processLaunchRequested());
    view.onEnter();
    assert(!view.handleInput(InputButton::A, InputEvent::Pressed) && !processLaunchRequested());
    write(folder / "config.json", native);
    view.onEnter();
    assert(!view.handleInput(InputButton::A, InputEvent::Pressed) && takeProcessLaunch());
    fs::remove_all(folder);
    view.onEnter();
    assert(!view.handleInput(InputButton::A, InputEvent::Pressed) && !processLaunchRequested());
    snapshot(view, renderer, font, theme, fs::path(argv[3]) / ("empty-" + std::to_string(width) + ".bmp"));
    assert(!view.handleInput(InputButton::B, InputEvent::Pressed));
    std::cout << "TreeFrog UI " << width << ": fresh entry, revalidation, helper-only argv, missing/invalid/empty states passed\n";
}
