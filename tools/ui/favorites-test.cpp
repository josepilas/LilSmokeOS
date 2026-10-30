#define SDL_MAIN_HANDLED
#include "FavoritesStore.h"
#include "FavoritesView.h"
#include "GameOptionsView.h"
#include "RecentGamesView.h"
#include "GridView.h"
#include "SettingsView.h"
#include "SystemCollections.h"
#include "ProcessLaunch.h"
#include <SDL2/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sys/file.h>

using namespace lilsmoke;
namespace fs = std::filesystem;
static unsigned checks = 0;
static void check(bool ok) { ++checks; if (!ok) { std::cerr << "check " << checks << " failed\n"; std::abort(); } }
static void put(const fs::path &path, const std::string &data) {
    fs::create_directories(path.parent_path()); std::ofstream out(path, std::ios::binary); out << data; check(bool(out));
}
static std::string get(const fs::path &path) {
    std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static void press(View &view, InputButton button) { view.handleInput(button, InputEvent::Pressed); }
namespace lilsmoke {
struct RendererTestAccess {
    static void init(Renderer &r, SDL_Surface *surface) {
        r.width_ = r.outputWidth_ = surface->w; r.height_ = r.outputHeight_ = surface->h;
        r.renderer_ = SDL_CreateSoftwareRenderer(surface); check(r.renderer_ != nullptr);
    }
};
}
int main(int argc, char **argv) {
    check(argc == 3); const fs::path base = argv[1]; check(!fs::exists(base)); fs::create_directories(base);
    const auto storage = base / "storage"; fs::create_directories(storage);
    const auto file = storage / ".lilsmoke/favorites.v1";
    FavoritesStore store(storage.string()); check(store.load() && store.entries().empty() && store.hideEmptySystems());
    check(!fs::exists(storage / ".lilsmoke"));
    ROM a; a.system = "nes"; a.name = "Same title"; a.path = (base / "quote \" and space.nes").string(); put(a.path, "fixture");
    ROM b = a; b.system = "snes"; b.path = (base / "B.sfc").string(); put(b.path, "fixture");
    check(store.setFavorite(a, true)); check(store.setFavorite(a, true)); check(store.entries().size() == 1);
    check(store.setFavorite(b, true)); check(store.entries().size() == 2);
    check(store.setHideEmptySystems(false));
    FavoritesStore stale(storage.string()); check(stale.load()); check(store.setFavorite(a, false));
    check(stale.setHideEmptySystems(true)); check(stale.entries().size() == 1 && stale.contains(b));
    check(store.load() && store.hideEmptySystems());
    const auto before = get(file);
    int lock = open((storage / ".lilsmoke/favorites.lock").c_str(), O_RDWR); check(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0);
    check(!store.setFavorite(a, true)); check(get(file) == before); close(lock);
    ROM invalid = a; invalid.path = "/bad/../noncanonical"; check(!store.setFavorite(invalid, true));
    invalid = a; invalid.name = "bad\nname"; check(!store.setFavorite(invalid, true));
    invalid = a; invalid.name.assign(257, 'x'); check(!store.setFavorite(invalid, true));
    fs::remove(b.path); check(!store.setFavorite(b, true)); check(store.setFavorite(b, false));
    check(store.entries().empty()); put(b.path, "fixture");
    std::vector<ROM> many;
    for (size_t i = 0; i < FavoritesStore::MaxEntries; ++i) {
        ROM rom = a; rom.path = (base / ("game-" + std::to_string(i) + ".nes")).string();
        put(rom.path, "fixture"); rom.name = std::string(240, 'W') + std::to_string(i);
        check(store.setFavorite(rom, true)); many.push_back(rom);
    }
    check(store.entries().size() == 128 && store.entries().size() > GameHistory::MaxEntries);
    const auto full = get(file); check(!store.setFavorite(a, true)); check(get(file) == full);
    GameHistory history(storage.string());
    for (int i = 0; i < 40; ++i) check(history.record({"nes", "/rom/" + std::to_string(i), "Recent"}, i));
    check(get(file) == full && store.load() && store.entries().size() == 128);
    check(store.setFavorite(many.back(), false)); check(store.setFavorite(a, true));
    check(fs::file_size(file) <= FavoritesStore::MaxBytes);
    for (const std::string bad : {"", "LILSMOKE_FAVORITES_2 1 0\nEND\n", "LILSMOKE_FAVORITES_1 2 0\nEND\n",
            "LILSMOKE_FAVORITES_1 1 -1\nEND\n", "LILSMOKE_FAVORITES_1 1 129\nEND\n",
            "LILSMOKE_FAVORITES_1 1 1\n\"nes\" \"/x\" \"Name\"\n",
            "LILSMOKE_FAVORITES_1 1 0\nEND\ntrailing",
            "LILSMOKE_FAVORITES_1 1 2\n\"nes\" \"/x\" \"Name\"\n\"nes\" \"/x\" \"Name\"\nEND\n"}) {
        put(file, bad); check(!store.load()); check(!store.setHideEmptySystems(false)); check(get(file) == bad);
    }
    put(file, std::string(FavoritesStore::MaxBytes + 1, 'x')); check(!store.load()); check(!store.setFavorite(a, true));
    fs::remove(file); fs::create_symlink(a.path, file); check(!store.load()); check(!store.setFavorite(a, true)); check(get(a.path) == "fixture");
    fs::remove(file); fs::create_directory(file); check(!store.load()); check(!store.setHideEmptySystems(false)); fs::remove(file);
    fs::remove_all(storage / ".lilsmoke"); fs::create_directory_symlink(base, storage / ".lilsmoke");
    check(!store.load() && !store.setFavorite(a, true)); fs::remove(storage / ".lilsmoke");
    FavoritesStore unavailable((base / "not-mounted").string()); check(!unavailable.setFavorite(a, true)); check(!fs::exists(base / "not-mounted"));
    check(store.setFavorite(a, true)); check(store.setFavorite(b, true));

    std::vector<SystemEntry> entries{{"nes", "NES", 2}, {"snes", "SNES", 0}, {"__applications", "Apps", 0},
        {"__opendingux", "OD", 0}, {"__recents", "Recents", 0}, {"__favorites", "Favorites", 0}, {"__settings", "Settings", 0}};
    auto all = entries; hideEmptyGameSystems(entries, true); check(entries.size() == all.size() - 1);
    check(entries[0].id == "nes" && entries[1].id == "__applications" && entries.back().id == "__settings");
    hideEmptyGameSystems(all, false); check(all.size() == 7);
    for (const int width : {320, 640}) {
        const auto previous = store.entries();
        for (const auto &rom : previous) check(store.setFavorite(rom, false));
        check(store.setFavorite(a, true)); check(store.setFavorite(b, true));
        check(SDL_Init(0) == 0);
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
            SDL_CreateRGBSurfaceWithFormat(0, width, width == 320 ? 240 : 480, 32, SDL_PIXELFORMAT_ARGB8888), SDL_FreeSurface);
        check(surface != nullptr); Renderer r; RendererTestAccess::init(r, surface.get());
        Font font; check(font.load(argv[2], 16)); Theme theme;
        unsigned launches = 0, options = 0, backs = 0;
        auto launch = [&](const ROM &rom) -> View * { check(rom.path == a.path || rom.path == b.path); ++launches; return nullptr; };
        auto option = [&](const ROM &) -> View * { ++options; return nullptr; };
        FavoritesView view(launch, option, store); view.onEnter();
        const std::string bytes = get(file);
        for (int i = 0; i < 120; ++i) { view.render(r, font, theme); r.present(); }
        check(get(file) == bytes); check(view.frameActivity(1000, theme).state == ViewActivity::State::Unknown);
        press(view, InputButton::A); check(launches == 1); press(view, InputButton::X); check(options == 1);
        fs::remove(a.path); press(view, InputButton::Start); check(launches == 1);
        press(view, InputButton::X); check(options == 2); // Removal remains accessible for missing games.
        GameOptionsView actions(a, launch, [&]() { ++backs; }, store); actions.onEnter();
        press(actions, InputButton::Down); press(actions, InputButton::A); check(store.load() && !store.contains(a));
        press(actions, InputButton::A); check(store.load() && !store.contains(a)); // Can't re-add a missing file.
        put(a.path, "fixture"); press(actions, InputButton::A); check(store.load() && store.contains(a));
        press(actions, InputButton::B); check(backs == 1 && !actions.allowAutoBackPop());
        actions.render(r, font, theme);
        PointerEvent pointer{}; pointer.type = PointerEventType::Pressed; pointer.button = PointerButton::Primary;
        pointer.x = 20; pointer.y = 85; actions.handlePointer(pointer); check(launches == 2);
        pointer.x = 0; actions.handlePointer(pointer); check(launches == 2);
        GameHistory recent(storage.string()); check(recent.record({a.system, a.path, a.name}, 1));
        RecentGamesView recentView(launch, recent); recentView.setOnOptions(option); recentView.onEnter();
        press(recentView, InputButton::X); check(options == 3);
        GridView grid({&a}, [](const ROM *) -> View * { return nullptr; }, "NES"); grid.setOnOptions(option);
        grid.onEnter(); press(grid, InputButton::X); check(options == 4);
        press(grid, InputButton::Y); check(grid.acceptsText()); press(grid, InputButton::X); check(options == 4);
        press(grid, InputButton::B); check(!grid.acceptsText());
        Config config; const auto configPath = base / ("config-" + std::to_string(width)); put(configPath, "SCAN_ROMS_ON_BOOT=off\n"); check(config.load(configPath.string()));
        SettingsView settings(config, store); bool notified = false; settings.setOnLibraryChanged([&](bool hide) { notified = !hide; }); settings.onEnter();
        // Hide Empty Systems follows Start in Recents, before Auto-sleep.
        for (int i = 0; i < 29; ++i) press(settings, InputButton::Down);
        press(settings, InputButton::Left);
        check(notified && store.load() && !store.hideEmptySystems()); check(get(configPath) == "SCAN_ROMS_ON_BOOT=off\n");
        press(settings, InputButton::Right); check(store.load() && store.hideEmptySystems());
        check(!config.getBool("RETROARCH_AUTO_RESUME", false));
        config.set("EMULATOR_nes", "ra:fceumm"); config.set("SCALING_MODE", "integer");
        const auto favoriteBytes = get(file);
        for (int i = 0; i < 7; ++i) press(settings, InputButton::Down);
        press(settings, InputButton::Right);
        check(config.getBool("RETROARCH_AUTO_RESUME", false));
        Config reread; check(reread.load(configPath.string()) && reread.getBool("RETROARCH_AUTO_RESUME", false));
        press(settings, InputButton::Left);
        check(!config.getBool("RETROARCH_AUTO_RESUME", true) && config.get("RETROARCH_AUTO_RESUME") == "off");
        check(reread.load(configPath.string()) && !reread.getBool("RETROARCH_AUTO_RESUME", true));
        check(config.get("EMULATOR_nes") == "ra:fceumm" && config.get("SCALING_MODE") == "integer");
        check(get(file) == favoriteBytes && !processLaunchRequested());
        check(store.setFavorite(many.front(), true));
        view.onEnter(); view.render(r, font, theme);
        check(SDL_SaveBMP(surface.get(), (base / ("favorites-" + std::to_string(width) + ".bmp")).c_str()) == 0);
    }
    std::cout << "FAVORITES_OK checks=" << checks << "\n";
}
