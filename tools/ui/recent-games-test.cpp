#define SDL_MAIN_HANDLED
#include "GameHistory.h"
#include "BootReadyMarker.h"
#include "RecentGamesView.h"
#include "Launcher.h"
#include "ProcessLaunch.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/file.h>
#include <sys/wait.h>

using namespace lilsmoke;
namespace fs = std::filesystem;
namespace lilsmoke {
struct RendererTestAccess {
    static bool init(Renderer &r, SDL_Surface *surface) {
        r.width_ = r.outputWidth_ = surface->w;
        r.height_ = r.outputHeight_ = surface->h;
        r.renderer_ = SDL_CreateSoftwareRenderer(surface);
        return r.renderer_ != nullptr;
    }
};
}
static unsigned checks = 0;
static void check(bool ok) { ++checks; if (!ok) { std::cerr << "check " << checks << " failed\n"; std::abort(); } }
static void put(const fs::path &path, const std::string &data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << data; check(bool(out));
}
static std::string get(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static void press(View &view, InputButton button) { view.handleInput(button, InputEvent::Pressed); }

int main(int argc, char **argv) {
    check(argc == 3);
    const fs::path base = argv[1];
    check(!fs::exists(base)); fs::create_directories(base);
    const auto root = base / "storage";
    fs::create_directories(root);
    GameHistory h(root.string());
    check(h.load() && h.entries().empty() && !h.startInRecents());
    check(!fs::exists(root / ".lilsmoke"));
    RecentGame a{"nes", (base / "A [quoted] \"x\".nes").string(), "Same title"};
    RecentGame b{"snes", (base / "B.sfc").string(), "Same title"};
    put(a.path, "fixture"); put(b.path, "fixture");
    check(h.record(a, 1234)); check(h.record(b, 4567));
    check(h.entries().size() == 2 && h.entries()[0].system == "snes");
    check(h.record(a, 500));
    check(h.entries()[0].totalMs == 1734 && h.entries()[0].lastMs == 500 && h.entries()[0].sessions == 2);
    check(h.setStartInRecents(true));
    GameHistory stale(root.string()); check(stale.load());
    check(h.record(b, 3));
    check(stale.setStartInRecents(false)); check(stale.entries()[0].totalMs == 4570);
    check(!stale.startInRecents());
    const auto file = root / ".lilsmoke/recent-games.v1";
    const std::string before = get(file);
    int lock = open((root / ".lilsmoke/recent-games.lock").c_str(), O_RDWR);
    check(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0);
    check(!h.record(a, 999)); check(get(file) == before); close(lock);
    check(!h.record({"nes", "relative", "Bad"}, 1));
    check(!h.record({"nes", a.path, std::string(257, 'x')}, 1));
    check(!h.record({"nes", a.path, "Bad\nname"}, 1));
    for (int i = 0; i < 40; ++i) check(h.record({"nes", "/fixture/" + std::to_string(i), "Bounded"}, i));
    check(h.entries().size() == 32 && h.entries().front().path == "/fixture/39" && h.entries().back().path == "/fixture/8");
    check(fs::file_size(file) <= GameHistory::MaxBytes);
    check(h.record(a, UINT64_MAX)); check(h.record(a, 1));
    check(h.entries()[0].totalMs == UINT64_MAX && h.entries()[0].lastMs == 1);
    for (const std::string bad : {"", "LILSMOKE_RECENTS_2 0\n", "LILSMOKE_RECENTS_1 2\n",
            "LILSMOKE_RECENTS_1 0\n-1 0 1 nes /x Name\n",
            "LILSMOKE_RECENTS_1 0\n18446744073709551616 0 1 nes /x Name\n",
            "LILSMOKE_RECENTS_1 0\n0 1 1 nes /x Name\n",
            "LILSMOKE_RECENTS_1 0\n1 1 1 nes /x Name\n1 1 1 nes /x Name\n"}) {
        put(file, bad); check(!h.load()); check(h.entries().empty() && !h.startInRecents());
    }
    put(file, std::string(GameHistory::MaxBytes + 1, 'x')); check(!h.load());
    check(h.record(a, 20)); // Explicit mutation repairs corrupt input.
    fs::remove(file); fs::create_symlink(a.path, file);
    check(!h.load()); check(!h.record(a, 20)); check(get(a.path) == "fixture");
    fs::remove(file); fs::create_directory(file);
    check(!h.record(a, 20)); fs::remove(file);
    fs::remove_all(root / ".lilsmoke");
    fs::create_directory_symlink(base, root / ".lilsmoke");
    check(!h.available() && !h.record(a, 1)); fs::remove(root / ".lilsmoke");
    check(sessionTime(0) == "0s" && sessionTime(61000) == "1m 1s" && sessionTime(3600000) == "1h 0m");
    check(runGameSession(a, "/does-not-exist", {"missing"}, h) == 127);
    check(h.load() && h.entries().empty());
    check(runGameSession(a, "/bin/sh", {"sh", "-c", "exit 7"}, h) == 7);
    check(h.load() && h.entries().empty());
    check(runGameSession(a, "/bin/sh", {"sh", "-c", "kill -TERM $$"}, h) == 143);
    check(h.load() && h.entries().empty());
    // The shell is STOPped for part of the session, then resumed by its child.
    check(runGameSession(a, "/bin/sh", {"sh", "-c", "(sleep 0.2; kill -CONT $$) & kill -STOP $$; wait"}, h) == 0);
    check(h.load() && h.entries().size() == 1 && h.entries()[0].lastMs >= 150);
    GameHistory unavailable((base / "not-mounted").string());
    check(runGameSession(a, "/bin/sh", {"sh", "-c", "exit 0"}, unavailable) == 0);
    check(!fs::exists(base / "not-mounted"));
    check(runGameSession({"nes", a.path, std::string(257, 'x')}, "/bin/sh", {"sh", "-c", "exit 0"}, h) == 0);
    check(h.record(b, 61000));

    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("LILSMOKE_UI_WINDOWED", "1", 1);
    setenv("LILSMOKE_UI_HIDDEN", "1", 1);
    for (const int width : {320, 640}) {
        check(SDL_Init(0) == 0);
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
            SDL_CreateRGBSurfaceWithFormat(0, width, width == 320 ? 240 : 480, 32, SDL_PIXELFORMAT_ARGB8888), SDL_FreeSurface);
        check(surface != nullptr);
        Renderer r; check(RendererTestAccess::init(r, surface.get()));
        Font font; check(font.load(argv[2], 16)); Theme theme;
        unsigned launched = 0;
        ROM last;
        RecentGamesView view([&](const ROM &rom) -> View * { ++launched; last = rom; return nullptr; }, h);
        view.onEnter();
        const std::string bytes = get(file);
        for (int i = 0; i < 120; ++i) { view.render(r, font, theme); r.present(); }
        check(get(file) == bytes); check(view.frameActivity(1000, theme).state == ViewActivity::State::Unknown);
        press(view, InputButton::A); check(launched == 1 && last.system == "snes" && last.path == b.path);
        fs::remove(b.path); press(view, InputButton::Start); check(launched == 1);
        view.render(r, font, theme); check(view.cursorAt(20, 48) == CursorKind::NotAllowed);
        put(b.path, "fixture"); press(view, InputButton::Start); check(launched == 2);
        press(view, InputButton::Down); check(view.selectedIndex() == 1);
        press(view, InputButton::B); check(view.allowAutoBackPop());
        PointerEvent pointer{}; pointer.type = PointerEventType::Pressed; pointer.button = PointerButton::Primary;
        pointer.x = 20; pointer.y = 48; view.handlePointer(pointer); check(launched == 3 && last.path == b.path);
        pointer.x = 0; view.handlePointer(pointer); check(launched == 3);
        for (int i = 0; i < 40; ++i) press(view, InputButton::Down);
        check(view.selectedIndex() == 1);
        SDL_Surface *capture = SDL_CreateRGBSurfaceWithFormat(0, r.outputWidth(), r.outputHeight(), 32, SDL_PIXELFORMAT_ARGB8888);
        check(capture != nullptr);
        view.render(r, font, theme);
        check(SDL_RenderReadPixels(static_cast<SDL_Renderer *>(r.sdlRenderer()), nullptr, capture->format->format, capture->pixels, capture->pitch) == 0);
        check(SDL_SaveBMP(capture, (base / ("recents-" + std::to_string(width) + ".bmp")).c_str()) == 0);
        SDL_FreeSurface(capture);
    }
    // Launch errors must not leave a pending process request or mutate metadata.
    ROM missing; missing.system = "nes"; missing.name = "Missing"; missing.path = "/missing/fixture.nes";
    Launcher launcher(missing); launcher.onEnter(); check(!processLaunchRequested());
    const fs::path jail = "/dev/shm/lilsmoke-recents-jail-" + std::to_string(getpid());
    check(!fs::exists(jail)); fs::create_directories(jail / "run");
    put(jail / "usr/lib/lilsmoke/lilsmoke-session", "fixture");
    chmod((jail / "usr/lib/lilsmoke/lilsmoke-session").c_str(), 0755);
    put(jail / "usr/lib/libretro/fceumm_libretro.so", "fixture");
    put(jail / "storage/roms/nes/game.nes", "fixture");
    const pid_t child = fork(); check(child >= 0);
    if (child == 0) {
        check(chroot(jail.c_str()) == 0 && chdir("/") == 0);
        setenv("LILSMOKE_BOOT_READY_FILE", "/run/not-allowed", 1);
        BootReadyMarker disabled; disabled.submitted(true); check(!fs::exists("/run/not-allowed"));
        setenv("LILSMOKE_BOOT_READY_FILE", "/run/lilsmoke-ui-ready", 1);
        BootReadyMarker ready; ready.submitted(false); check(!fs::exists("/run/lilsmoke-ui-ready"));
        ready.submitted(true); check(get("/run/lilsmoke-ui-ready") == "UI_FIRST_SUBMITTED_FRAME\n");
        put("/run/lilsmoke-ui-ready", "one shot"); ready.submitted(true);
        check(get("/run/lilsmoke-ui-ready") == "one shot");
        ROM game; game.system = "nes"; game.name = "Fixture"; game.path = "/storage/roms/nes/game.nes";
        Launcher normal(game); normal.onEnter();
        const auto launch = takeProcessLaunch(); check(launch.has_value());
        check(launch->executable == "/usr/lib/lilsmoke/lilsmoke-session");
        check(launch->arguments == std::vector<std::string>({"lilsmoke-session", "nes", game.path, "Fixture",
            "/usr/lib/lilsmoke/lilsmoke-launch", "lilsmoke-launch", "nes", game.path}));
        put("/persist/config/lilsmoke.conf", "EMULATOR_nes=ra:missing\n");
        Launcher invalidCore(game); invalidCore.onEnter(); check(!processLaunchRequested());
        _exit(0);
    }
    int status = 0; check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    fs::remove_all(jail);
    std::cout << "RECENT_GAMES_OK checks=" << checks << "\n";
}
