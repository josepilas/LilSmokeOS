#include "ApplicationsView.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace lilsmoke;
static bool testClock = false;
static Uint32 testTicks = 0;
extern "C" Uint32 __real_SDL_GetTicks();
extern "C" Uint32 __wrap_SDL_GetTicks() {
  return testClock ? testTicks : __real_SDL_GetTicks();
}
namespace lilsmoke {
struct ApplicationsTestAccess {
  static bool ready(ApplicationInstallerView &v) {
    return v.state_ == ApplicationInstallerView::State::Ready;
  }
  static bool done(ApplicationInstallerView &v) {
    return v.state_ == ApplicationInstallerView::State::Done;
  }
  static std::string error(ApplicationInstallerView &v) {
    return v.state_ == ApplicationInstallerView::State::Error ? v.message_ : "";
  }
  static std::string message(ApplicationInstallerView &v) { return v.message_; }
  static void progress(ApplicationInstallerView &v) {
    v.state_ = ApplicationInstallerView::State::Installing;
    v.percent_ = 41;
    v.processed_ = 21300000;
    v.total_ = 51800000;
  }
  static size_t count(ApplicationsView &v) { return v.apps_.size(); }
  static bool idle(ApplicationsView &v) {
    return !v.job_.running() && v.mode_ == ApplicationsView::Mode::Grid;
  }
  static bool renaming(ApplicationsView &v) {
    return v.mode_ == ApplicationsView::Mode::Rename;
  }
  static size_t selected(ApplicationsView &v) { return v.selected_; }
  static void resources(ApplicationInstallerView &v) {
    assert(v.artwork_.retainedPixelBytes() < 4 * 1024 * 1024);
    std::cout << "ARTWORK_RETAINED_RGBA_BYTES "
              << v.artwork_.retainedPixelBytes() << "\n";
  }
  static void animation(ApplicationInstallerView &v, Renderer &r) {
    testClock = true;
    unsigned before = v.artwork_.cubeRedraws();
    for (testTicks = 1000; testTicks < 2000; ++testTicks)
      v.artwork_.cube(r, "", false, 160, 120, 100);
    unsigned frames = v.artwork_.cubeRedraws() - before;
    assert(frames >= 60 && frames <= 63);
    before = v.artwork_.cubeRedraws();
    for (unsigned i = 0; i < 100; ++i)
      v.artwork_.cube(r, "", false, 160, 120, 100);
    assert(v.artwork_.cubeRedraws() == before);
    testTicks = 0xfffffff0U;
    v.artwork_.cube(r, "", false, 160, 120, 100);
    before = v.artwork_.cubeRedraws();
    testTicks = 4;
    v.artwork_.cube(r, "", false, 160, 120, 100);
    assert(v.artwork_.cubeRedraws() == before + 1);
    testClock = false;
    std::cout << "CUBE_TIMING redraws_per_simulated_second=" << frames
              << " cached_same_tick=1 rollover=1\n";
  }
};
} // namespace lilsmoke
static void screenshot(Renderer &r, const std::string &name) {
  int width = 0, height = 0;
  assert(!SDL_GetRendererOutputSize(
      static_cast<SDL_Renderer *>(r.sdlRenderer()), &width, &height));
  auto s = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                          SDL_PIXELFORMAT_ARGB8888);
  assert(s);
  assert(!SDL_RenderReadPixels(static_cast<SDL_Renderer *>(r.sdlRenderer()),
                               nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels,
                               s->pitch));
  assert(!SDL_SaveBMP(s, name.c_str()));
  SDL_FreeSurface(s);
}
template <class Predicate>
static void wait(View &v, Renderer &r, Font &font, const Theme &theme,
                 Predicate condition) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (!condition()) {
    assert(std::chrono::steady_clock::now() < end);
    v.render(r, font, theme);
    r.present();
    SDL_Delay(10);
  }
}
int main(int argc, char **argv) {
  assert(argc == 8);
  Renderer r;
  assert(r.init(std::stoi(argv[1]), std::stoi(argv[2])));
  Font font;
  assert(font.load(argv[5], 16));
  Theme theme;
  std::filesystem::create_directories(argv[4]);
  std::string out = argv[4];
  bool returned = false;
  {
    ApplicationInstallerView installer(argv[3]);
    installer.setOnBack([&]() -> View * {
      returned = true;
      return nullptr;
    });
    installer.onEnter();
    wait(installer, r, font, theme, [&] {
      return ApplicationsTestAccess::ready(installer) ||
             !ApplicationsTestAccess::error(installer).empty();
    });
    assert(ApplicationsTestAccess::error(installer).empty());
    installer.render(r, font, theme);
    r.present();
    screenshot(r, out + "/installer.bmp");
    ApplicationsTestAccess::resources(installer);
    ApplicationsTestAccess::animation(installer, r);
    installer.handleInput(InputButton::B, InputEvent::Pressed);
    assert(returned);
  }
  {
    ApplicationInstallerView installer(argv[3]);
    installer.onEnter();
    wait(installer, r, font, theme,
         [&] { return ApplicationsTestAccess::ready(installer); });
    installer.handleInput(InputButton::Left, InputEvent::Pressed);
    installer.handleInput(InputButton::A, InputEvent::Pressed);
    wait(installer, r, font, theme, [&] {
      return ApplicationsTestAccess::done(installer) ||
             !ApplicationsTestAccess::error(installer).empty();
    });
    assert(ApplicationsTestAccess::error(installer).empty());
    ApplicationsTestAccess::progress(installer);
    installer.render(r, font, theme);
    r.present();
    screenshot(r, out + "/progress.bmp");
  }
  {
    ApplicationInstallerView installer(argv[3]);
    installer.onEnter();
    wait(installer, r, font, theme,
         [&] { return ApplicationsTestAccess::done(installer); });
    assert(ApplicationsTestAccess::message(installer) == "Already installed.");
    installer.render(r, font, theme);
    r.present();
    screenshot(r, out + "/already-installed.bmp");
  }
  for (int index = 6; index < 8; ++index) {
    ApplicationInstallerView installer(argv[index]);
    installer.onEnter();
    wait(installer, r, font, theme,
         [&] { return ApplicationsTestAccess::ready(installer); });
    assert(ApplicationsTestAccess::message(installer) ==
           "This package is a different version from yours. Would you like to "
           "update?");
    installer.render(r, font, theme);
    r.present();
    screenshot(r,
               out + (index == 6 ? "/newer-update.bmp" : "/older-update.bmp"));
    installer.handleInput(InputButton::Left, InputEvent::Pressed);
    installer.handleInput(InputButton::A, InputEvent::Pressed);
    wait(installer, r, font, theme, [&] {
      return ApplicationsTestAccess::done(installer) ||
             !ApplicationsTestAccess::error(installer).empty();
    });
    assert(ApplicationsTestAccess::error(installer).empty());
  }
  ApplicationsView grid;
  grid.onEnter();
  wait(grid, r, font, theme,
       [&] { return ApplicationsTestAccess::count(grid) > 0; });
  grid.render(r, font, theme);
  r.present();
  screenshot(r, out + "/grid.bmp");
  grid.handleInput(InputButton::Y, InputEvent::Pressed);
  grid.render(r, font, theme);
  r.present();
  screenshot(r, out + "/options.bmp");
  grid.handleInput(InputButton::A, InputEvent::Pressed);
  assert(ApplicationsTestAccess::renaming(grid));
  grid.handleText(std::string(grid.textValue().size(), '\b') +
                  "Renamed Application");
  grid.handleInput(InputButton::Start, InputEvent::Pressed);
  wait(grid, r, font, theme,
       [&] { return ApplicationsTestAccess::idle(grid); });
  assert(std::filesystem::exists(
      std::filesystem::path(getenv("LILSMOKE_APPS_ROOT")) /
      "Renamed Application/meta.json"));
  grid.render(r, font, theme);
  r.present();
  screenshot(r, out + "/renamed.bmp");
  grid.handleInput(InputButton::Y, InputEvent::Pressed);
  grid.handleInput(InputButton::Down, InputEvent::Pressed);
  grid.handleInput(InputButton::A, InputEvent::Pressed);
  grid.render(r, font, theme);
  r.present();
  screenshot(r, out + "/uninstall-confirm.bmp");
  grid.handleInput(InputButton::Left, InputEvent::Pressed);
  grid.handleInput(InputButton::A, InputEvent::Pressed);
  wait(grid, r, font, theme,
       [&] { return ApplicationsTestAccess::idle(grid); });
  assert(ApplicationsTestAccess::count(grid) == 0);
  for (int i = 0; i < 24; ++i) {
    auto dir = std::filesystem::path(getenv("LILSMOKE_APPS_ROOT")) /
               ("Application " + std::to_string(i));
    std::filesystem::create_directory(dir);
    std::ofstream(dir / "meta.json")
        << "{\"name\":\"Application " << i
        << "\",\"version\":\"1.0.0\",\"id\":\"org.test.grid" << i << "\""
        << (i % 3 == 0 ? ",\"icon\":\"icon.png\"" : "") << "}";
    if (i % 3 == 0) {
      auto p =
          std::filesystem::path(argv[3]).parent_path() / "fixture-icon.png";
      std::filesystem::copy_file(p, dir / "icon.png");
    }
    std::ofstream(dir / "run.sh") << "#!/bin/sh\nexit 0\n";
  }
  grid.handleInput(InputButton::X, InputEvent::Pressed);
  wait(grid, r, font, theme,
       [&] { return ApplicationsTestAccess::count(grid) == 24; });
  grid.render(r, font, theme);
  r.present();
  screenshot(r, out + "/grid-many.bmp");
  auto themePath = out + "/contrasting-theme.xml";
  std::ofstream(themePath)
      << "<theme><formatVersion>4</formatVersion><view name=\"system\"><image "
         "name=\"background\"><color>FF0000</color></image></view></theme>";
  Theme contrasting;
  assert(contrasting.load(themePath));
  grid.render(r, font, contrasting);
  r.present();
  screenshot(r, out + "/grid-theme-independent.bmp");
  grid.handleInput(InputButton::R1, InputEvent::Pressed);
  assert(ApplicationsTestAccess::selected(grid) > 0);
  grid.render(r, font, theme);
  r.present();
  screenshot(r, out + "/grid-next-page.bmp");
  std::cout << "PASS native application "
               "installer/cancel/install/grid/rename/uninstall UI\n";
}
