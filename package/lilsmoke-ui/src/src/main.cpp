// LilSmokeUI — main entry point
//
// Boots the SDL2/DirectFB UI. The flow is:
//
//   1. Initialize Renderer (SDL2 + DirectFB)
//   2. Initialize Font (DejaVu Sans @ 16pt)
//   3. Load Theme (from /usr/share/lilsmoke/themes/default/theme.xml,
//                 or override from /persist/config/lilsmoke.conf DEFAULT_THEME)
//   4. Initialize Input (evdev /dev/input/event*)
//   5. Scan ROMs (/storage/roms/<system>/*)
//   6. Push the SystemListView onto the view stack
//   7. Main loop:
//        - Poll input
//        - Dispatch to top view
//        - If view returns non-null, push it
//        - If user backs out of root view, exit
//        - Render top view
//        - Present + cap total frame time
//
// The whole binary is ~700 KB stripped. Static-linked SDL2 would shave
// another ~50 KB off the .so overhead but complicates the build — defer
// to J7 perf pass.

#include "Config.h"
#include "BootReadyMarker.h"
#include "Cursor.h"
#include "RemoteAccessView.h"
#include "FileExplorerView.h"
#include "ApplicationsView.h"
#include "Font.h"
#include "FramePacer.h"
#include "FrameActivityProfile.h"
#include "UiResolution.h"
#include "UiWarmReturn.h"
#include "GridView.h"
#include "Input.h"
#include "InputSettingsView.h"
#include "Launcher.h"
#include "RecentGamesView.h"
#include "FavoritesView.h"
#include "GameOptionsView.h"
#include "SystemCollections.h"
#include "ProcessLaunch.h"
#include "PointerMotionBatch.h"
#include "ListView.h"
#include "TreeFrogPortsView.h"
#include "Log.h"
#include "LoadingProgress.h"
#include "Renderer.h"
#include "RetroArchMenuView.h"
#include "RetroArchPlaylists.h"
#include "ROMScanner.h"
#include "SettingsView.h"
#include "EmulatorSettingsView.h"
#include "BluetoothView.h"
#include "SystemView.h"
#include "OTAView.h"
#include "Theme.h"
#include "View.h"
#include "VirtualKeyboard.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace lilsmoke {

// Display names for each system (matches lilsmoke-launch routing)
static std::string systemDisplayName(const std::string &sys) {
    if (sys == "nes")    return "NES / Famicom";
    if (sys == "snes")   return "SNES / Super Famicom";
    if (sys == "gb")     return "Game Boy";
    if (sys == "gbc")    return "Game Boy Color";
    if (sys == "gba")    return "Game Boy Advance";
    if (sys == "md")     return "Genesis / Mega Drive";
    if (sys == "sms")    return "Master System";
    if (sys == "gg")     return "Game Gear";
    if (sys == "pce")    return "PC Engine / TG16";
    if (sys == "arcade") return "Arcade (FBNeo)";
    if (sys == "lynx")   return "Atari Lynx";
    if (sys == "psx")    return "PlayStation (PS1)";
    if (sys == "n64")    return "Nintendo 64";
    if (sys == "portsmoke") return "PortSmoke";
    return emulatorSystemName(sys);
}

static bool isRetroArchSystem(const std::string &sys) {
    for (const auto &provider : emulatorProviders(false))
        if (provider.backend == "retroarch" && provider.available() && provider.supports(sys)) return true;
    return false;
}

static bool fileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool dirExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string joinPath(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    char last = a[a.size() - 1];
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

static std::vector<std::string> systemThemeAliases(const std::string &sys) {
    if (sys == "md") return {"megadrive", "genesis", "md"};
    if (sys == "pce") return {"pcengine", "tg16", "pce"};
    if (sys == "sms") return {"mastersystem", "sms"};
    if (sys == "gg") return {"gamegear", "gg"};
    if (sys == "psx") return {"psx", "ps1", "playstation"};
    if (sys == "arcade") return {"arcade", "fbneo", "fba", "mame"};
    if (sys == "portsmoke") return {"portsmoke", "ports", "pc"};
    if (sys == "snes") return {"snes", "sfc"};
    if (sys == "gb") return {"gb", "gameboy"};
    if (sys == "gbc") return {"gbc", "gbcolor"};
    if (sys == "gba") return {"gba", "gbadvance"};
    return {sys};
}

static std::string resolveThemeSetDir(const std::string &themeName) {
    std::vector<std::string> roots = {
        "/storage/themes",
        "/storage/.config/emulationstation/themes",
        "/roms/themes",
        "/userdata/themes",
        "/usr/share/lilsmoke/themes",
    };

    for (const auto &root : roots) {
        std::string path = joinPath(root, themeName);
        if (dirExists(path)) return path;
    }
    return "";
}

static std::string resolveSystemThemeName(const std::string &themeSetDir,
                                          const std::string &system) {
    auto aliases = systemThemeAliases(system);
    for (const auto &alias : aliases) {
        std::string themeXml = joinPath(joinPath(themeSetDir, alias), "theme.xml");
        if (fileExists(themeXml)) return alias;
    }
    return aliases.empty() ? system : aliases.front();
}

class App {
public:
    explicit App(bool warmReturn);
    int run();

private:
    Config config_;
    Renderer renderer_;
    Font font_;
    Theme theme_;
    std::string themeSetDir_;
    Input input_;
    CursorAtlas cursors_;
    VirtualKeyboard keyboard_;
    View *keyboardOwner_ = nullptr;
    ROMScanner scanner_;
    bool scanRequested_ = false;
    bool warmReturn_ = false;
    bool libraryChanged_ = false, hideEmptySystems_ = true, homeEntriesValid_ = false;
    std::vector<SystemEntry> homeGameEntries_;
    FrameReuseGuard frameReuse_;
    int refreshGames(bool force);

    std::vector<std::unique_ptr<View>> viewStack_;

    View *current() { return viewStack_.empty() ? nullptr : viewStack_.back().get(); }

    void push(std::unique_ptr<View> v) {
        frameReuse_.invalidate();
        if (current()) current()->onLeave();
        viewStack_.push_back(std::move(v));
        current()->onEnter();
    }
    void loadTheme(const std::string &themeName);
    void applyPresentation();

    void pop() {
        frameReuse_.invalidate();
        if (!viewStack_.empty()) {
            current()->onLeave();
            viewStack_.pop_back();
            if (viewStack_.size() == 1 && libraryChanged_) {
                viewStack_[0] = makeSystemListView("__settings");
                libraryChanged_ = false;
            }
            if (current()) current()->onEnter();
        }
    }

    // Build the system list view (root of navigation)
    std::unique_ptr<View> makeSystemListView(const std::string &focus = "");
    std::unique_ptr<View> makeRecentGamesView();
    View *gameOptions(const ROM &rom);
    std::unique_ptr<View> makeExplorerView(const std::string &path);

    // Build the RetroArch app view
    std::unique_ptr<View> makeRetroArchAppView();
    std::unique_ptr<View> makeOpenDinguxView();

    // Build a game list view for a given system
    std::unique_ptr<View> makeGameListView(const std::string &system);
};

App::App(bool warmReturn) : warmReturn_(warmReturn) {
    // Set env defaults if not already set
    setenv("SDL_VIDEODRIVER", "directfb", 0);
    setenv("SDL_AUDIODRIVER", "alsa", 0);
}

std::unique_ptr<View> App::makeExplorerView(const std::string &directory) {
    // Explorer has no mutation callback. Conservatively refresh on leaving it,
    // and retain invalidation across a media/application exec from Explorer.
    if (!durableReplace(uiLibraryDirty, "explorer\n"))
        unlink("/persist/cache/roms.index");
    unlink(uiPlaylistState);
    auto view = std::make_unique<FileExplorerView>(directory);
    view->setOnApplicationPackage([this](const std::string &path) {
        auto installer = std::make_unique<ApplicationInstallerView>(path);
        installer->setOnBack([this]() -> View * { pop(); return nullptr; });
        push(std::move(installer));
    });
    view->setOnPackageApplied([this]() {
        config_.load("/persist/config/lilsmoke.conf");
        applyPresentation();
    });
    view->setOnBack([this]() -> View * { pop(); scanRequested_ = true; return nullptr; });
    const char *focus = std::getenv("LILSMOKE_EXPLORER_FOCUS");
    if (focus) view->focusPath(focus);
    return view;
}

View *App::gameOptions(const ROM &rom) {
    return new GameOptionsView(rom, [](const ROM &game) -> View * { return new Launcher(game); }, [this]() { pop(); });
}

std::unique_ptr<View> App::makeRecentGamesView() {
    auto view = std::make_unique<RecentGamesView>([](const ROM &rom) -> View * { return new Launcher(rom); });
    view->setOnOptions([this](const ROM &rom) { return gameOptions(rom); });
    return view;
}

std::unique_ptr<View> App::makeSystemListView(const std::string &focus) {
    if (!homeEntriesValid_) {
        homeGameEntries_.clear();
        const auto providers = emulatorProviders();
        const auto supported = configurableEmulatorSystems(providers);
        for (const auto &system : scanner_.systems()) {
            if (system == "portsmoke" || std::find(supported.begin(), supported.end(), system) == supported.end()) continue;
            homeGameEntries_.push_back({system, systemDisplayName(system), static_cast<int>(scanner_.bySystem(system).size())});
        }
        // Cache eligible empty systems once alongside the existing ROM index.
        for (const auto &system : supported) {
            if (system == "portsmoke" || std::any_of(homeGameEntries_.begin(), homeGameEntries_.end(),
                [&](const auto &entry) { return entry.id == system; })) continue;
            if (std::any_of(providers.begin(), providers.end(), [&](const auto &provider) {
                return provider.available() && provider.needsRom && provider.supports(system);
            })) homeGameEntries_.push_back({system, systemDisplayName(system), 0});
        }
        homeEntriesValid_ = true;
    }
    std::vector<SystemEntry> entries = homeGameEntries_;
    entries.push_back({"__applications", "LilSmoke Applications", 0});
    entries.push_back({"__treefrog", "TreeFrog Ports", 0});
    entries.push_back({"__recents", "Recent Games", 0});
    entries.push_back({"__favorites", "Favorites", 0});
    entries.push_back({"__retroarch", "RetroArch", 0});
    entries.push_back({"__explorer", "LilSmoke Explorer", 0});
    entries.push_back({"__opendingux", "OpenDingux", 0});
    entries.push_back({"__settings", "Settings", 0});
    entries.push_back({"__exit", "Exit", 0});
    hideEmptyGameSystems(entries, hideEmptySystems_);
    int selected = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].id == focus) selected = static_cast<int>(i);
        LOG_INFO("Home: entry=%s games=%d hide_empty=%d", entries[i].id.c_str(), entries[i].gameCount, hideEmptySystems_);
    }

    auto root = std::make_unique<SystemView>(
        std::move(entries),
        [this](const SystemEntry &entry) -> View * {
            if (entry.id == "__recents") {
                push(makeRecentGamesView());
                return nullptr;
            }
            if (entry.id == "__favorites") {
                push(std::make_unique<FavoritesView>([](const ROM &rom) -> View * { return new Launcher(rom); },
                    [this](const ROM &rom) { return gameOptions(rom); }));
                return nullptr;
            }
            if (entry.id=="__applications") {
                auto view=std::make_unique<ApplicationsView>();
                view->setOnBack([this]() -> View * {pop();return nullptr;});
                push(std::move(view));return nullptr;
            }
            if (entry.id == "__treefrog") {
                push(std::make_unique<TreeFrogPortsView>());
                return nullptr;
            }
            if (entry.id == "__settings") {
                // Settings
                auto sv = std::make_unique<SettingsView>(config_);
                sv->setOnLibraryChanged([this](bool hide) { hideEmptySystems_ = hide; libraryChanged_ = true; });
                sv->setOnScanGames([this]() { scanRequested_ = true; });
                sv->setOnOpenEmulators([this]() -> View * {
                    push(std::make_unique<EmulatorSettingsView>(config_));
                    return nullptr;
                });
                sv->setOnPowerOff([this]() {
                    config_.save();
                    requestProcessLaunch({"/usr/lib/lilsmoke/lilsmoke-power",
                                          {"lilsmoke-power", "poweroff"}});
                });
                sv->setOnThemeChanged([this](const std::string &name) {
                    loadTheme(name);
                });
                sv->setOnPointerChanged([this](bool enabled, const std::string &speed) {
                    input_.setPointerOptions(enabled, speed);
                });
                sv->setOnPresentationChanged([this]() { applyPresentation(); });
                sv->setOnOpenRemote([this]() -> View * {
                    auto remote = std::make_unique<RemoteAccessView>(config_);
                    push(std::move(remote));
                    return nullptr;
                });
                sv->setOnBack([this]() -> View * {
                    pop();
                    return nullptr;
                });
                sv->setOnOpenBluetooth([this]() -> View * {
                    auto view = std::make_unique<BluetoothView>(config_);
                    view->setOnBack([this]() -> View * { pop(); return nullptr; });
                    push(std::move(view)); return nullptr;
                });
                sv->setOnOpenInput([this]() -> View * {
                    auto iv = std::make_unique<InputSettingsView>(input_);
                    iv->setOnBack([this]() -> View * {
                        // Save user map on exit
                        input_.keymap().saveUserMap(
                            "/persist/config/input-maps/user.map");
                        pop();
                        return nullptr;
                    });
                    push(std::move(iv));
                    return nullptr;
                });
                sv->setOnOpenOTA([this]() -> View * {
                    auto ov = std::make_unique<OTAView>(config_);
                    ov->setOnBack([this]() -> View * {
                        pop();
                        return nullptr;
                    });
                    push(std::move(ov));
                    return nullptr;
                });
                push(std::move(sv));
                return nullptr;
            }
            if (entry.id == "__explorer") {
                push(makeExplorerView("/storage"));
                return nullptr;
            }
            if (entry.id == "__retroarch") {
                push(makeRetroArchAppView());
                return nullptr;
            }
            if (entry.id == "__opendingux") {
                push(makeOpenDinguxView());
                return nullptr;
            }
            if (entry.id == "__exit") {
                // Exit
                exit(0);
            }
            push(makeGameListView(entry.id));
            return nullptr;
        },
        &themeSetDir_);
    root->setSelectedIndex(selected);
    return root;
}

std::unique_ptr<View> App::makeRetroArchAppView() {
    std::vector<std::string> retroSystems;
    for (const auto &system : scanner_.systems()) {
        if (isRetroArchSystem(system)) retroSystems.push_back(system);
    }

    std::vector<std::string> items;
    items.push_back("Open RetroArch Menu");
    for (const auto &system : retroSystems) {
        items.push_back(systemDisplayName(system));
    }
    if (retroSystems.empty()) {
        items.push_back("No RetroArch games found");
    }

    return std::make_unique<ListView>(
        std::move(items),
        [this, retroSystems](int idx, const std::string &) -> View * {
            if (idx == 0) {
                push(std::make_unique<RetroArchMenuView>());
                return nullptr;
            }

            int sysIdx = idx - 1;
            if (sysIdx >= 0 && sysIdx < (int)retroSystems.size()) {
                push(makeGameListView(retroSystems[sysIdx]));
            }
            return nullptr;
        },
        "RetroArch");
}

std::unique_ptr<View> App::makeOpenDinguxView() {
    std::vector<EmulatorProvider> apps;
    std::vector<std::string> items;
    for (const auto &p : emulatorProviders()) {
        if (p.backend != "opendingux" || (p.id.rfind("od:", 0) == 0 && !p.available())) continue;
        apps.push_back(p);
        items.push_back(p.name + (p.available() ? "" : " (incompatible)"));
    }
    if (apps.empty()) items.push_back("No OpenDingux applications installed");
    return std::make_unique<ListView>(std::move(items),
        [this, apps](int index, const std::string &) -> View * {
            if (index < 0 || index >= static_cast<int>(apps.size())) return nullptr;
            const auto app = apps[index];
            if (!app.available()) {
                push(std::make_unique<ListView>(std::vector<std::string>{app.reason},
                    [](int, const std::string &) -> View * { return nullptr; }, app.name, false));
                return nullptr;
            }
            requestProcessLaunch({"/usr/lib/lilsmoke/lilsmoke-emulators",
                                  {"lilsmoke-emulators", "menu", app.id}});
            return nullptr;
        }, "OpenDingux", false);
}

std::unique_ptr<View> App::makeGameListView(const std::string &system) {
    auto roms = scanner_.bySystem(system);
    std::vector<const ROM *> romPtrs;
    for (auto r : roms) romPtrs.push_back(r);
    const size_t gameCount = romPtrs.size();

    Theme systemTheme;
    if (!themeSetDir_.empty()) {
        std::string themeName = resolveSystemThemeName(themeSetDir_, system);
        systemTheme.loadSet(themeSetDir_, themeName, systemDisplayName(system));
    }

    auto view = std::make_unique<GridView>(
        std::move(romPtrs),
        [this](const ROM *rom) -> View * {
            push(std::make_unique<Launcher>(*rom));
            return nullptr;
        },
        systemDisplayName(system),
        std::move(systemTheme));
    view->setOnOptions([this](const ROM &rom) { return gameOptions(rom); });
    LOG_INFO("GameList: ready for %s (%zu games)",
             system.c_str(), gameCount);
    return view;
}

void App::loadTheme(const std::string &themeName) {
    frameReuse_.invalidate();
    const std::string transition = config_.get("THEME_TRANSITION", "auto");
    setenv("LILSMOKE_THEME_TRANSITION", transition.c_str(), 1);
    std::string subsetPreferences = config_.get("THEME_SUBSETS", "");
    auto addSubset = [&subsetPreferences](const std::string &name,
                                          const std::string &value) {
        if (value.empty() || value == "theme-default") return;
        if (!subsetPreferences.empty()) subsetPreferences += ";";
        subsetPreferences += name + "=" + value;
    };
    const std::string systemView =
        config_.get("THEME_SYSTEM_VIEW", "theme-default");
    const std::string gameListView =
        config_.get("THEME_GAMELIST_VIEW", "theme-default");
    addSubset("system-view-style", systemView);
    addSubset("systemview", systemView);
    addSubset("gamelist-view-style", gameListView);
    addSubset("gamelistview", gameListView);
    if (subsetPreferences.empty()) unsetenv("LILSMOKE_THEME_SUBSETS");
    else setenv("LILSMOKE_THEME_SUBSETS", subsetPreferences.c_str(), 1);

    theme_ = Theme();
    themeSetDir_ = resolveThemeSetDir(themeName);
    if (!themeSetDir_.empty()) {
        if (!theme_.loadSet(themeSetDir_))
            LOG_WARN("Could not load theme set %s", themeSetDir_.c_str());
    } else {
        LOG_WARN("Could not find theme set %s; using built-in colors",
                 themeName.c_str());
    }
}

void App::applyPresentation() {
    frameReuse_.invalidate();
    const bool tv = config_.get("DEVICE_MODE", "handheld") == "tv";
    renderer_.setSafeAreaPercent(tv ? config_.getInt("TV_SAFE_AREA_PERCENT", 5) : 0);
    input_.setPointerViewport(renderer_.outputWidth(), renderer_.outputHeight(),
                              renderer_.insetX(), renderer_.insetY(),
                              renderer_.width(), renderer_.height());
    const std::string battery = tv ? "off" : config_.get("SHOW_BATTERY", "text");
    setenv("LILSMOKE_SHOW_BATTERY", battery.c_str(), 1);
}

static std::string playlistSnapshot(const std::string &cache, const std::vector<ROM> &roms) {
    UiPlaylistSnapshot snapshot;
    snapshot.file(cache);
    // Cache loading can prune PSX references. Witness the effective playlist
    // records as well as the cache file, without retaining a second ROM list.
    uint64_t records = 14695981039346656037ULL;
    for (const auto &rom : roms) {
        for (const auto *field : {&rom.system, &rom.path, &rom.name}) {
            for (unsigned char c : *field) { records ^= c; records *= 1099511628211ULL; }
            records ^= 0; records *= 1099511628211ULL;
        }
    }
    snapshot.value(std::to_string(roms.size()) + ":" + std::to_string(records));
    snapshot.file("/proc/self/exe");
    snapshot.file("/persist/config/lilsmoke.conf");
    for (const char *name : {"LILSMOKE_SYSTEM_CONFIG", "LILSMOKE_CONFIG",
                            "LILSMOKE_CORES_DIR", "LILSMOKE_EXTRA_CORES_DIR"}) {
        const char *value = std::getenv(name);
        snapshot.value(value ? value : "");
        if (value) snapshot.file(value);
    }
    for (const auto &provider : emulatorProviders(false)) {
        if (provider.backend != "retroarch") continue;
        snapshot.value(provider.id);
        snapshot.value(provider.reason);
        snapshot.file(provider.path);
        // The playlist writer also checks the default core directory.
        snapshot.file("/usr/lib/libretro/" + provider.argument);
    }
    for (const auto &system : emulatorSystems())
        snapshot.file("/persist/playlists/LilSmoke-" + system + ".lpl");
    return snapshot.data();
}

int App::refreshGames(bool force) {
    homeEntriesValid_ = false;
    const std::string root = config_.get("ROMS_DIR", "/storage/roms");
    const std::string cache = "/persist/cache/roms.index";
    std::error_code ec;
    if (fileExists("/run/lilsmoke-storage/exporting") ||
        !std::filesystem::is_directory(root, ec)) {
        LOG_WARN("Library: scan deferred; ROM storage is unavailable");
        return static_cast<int>(scanner_.roms().size());
    }
    const bool warm = std::exchange(warmReturn_, false);
    const bool dirty = access(uiLibraryDirty, F_OK) == 0;
    const bool cached = warmReturnMayLoadCache(force, config_.getBool("SCAN_ROMS_ON_BOOT", true), warm, dirty) &&
        scanner_.loadCache(cache, root);
    if (!cached) {
        LOG_INFO("Library: %s scan", force ? "manual" : "boot");
        scanner_.scan(root);
        if (!scanner_.saveCache(cache)) {
            LOG_WARN("Library: cache save failed");
            unlink(cache.c_str());
        }
        else unlink(uiLibraryDirty);
    } else LOG_INFO("Library: using cached game index%s", warm ? " (supervisor warm return)" : "");
    if (warm && cached && unchangedUiPlaylists(playlistSnapshot(cache, scanner_.roms()))) {
        LOG_INFO("Library: unchanged warm playlists retained");
    } else {
        unlink(uiPlaylistState);
        if (!writeRetroArchPlaylists(scanner_.roms()))
            LOG_WARN("Library: RetroArch playlist refresh failed");
        else {
            const std::string snapshot = playlistSnapshot(cache, scanner_.roms());
            if (!snapshot.empty()) durableReplace(uiPlaylistState, snapshot);
            LOG_INFO("Library: RetroArch playlists refreshed");
        }
    }
    LOG_INFO("Library: %d games ready", static_cast<int>(scanner_.roms().size()));
    return static_cast<int>(scanner_.roms().size());
}

int App::run() {
    LOG_INFO("LilSmokeUI v0.1 starting");
    if (config_.get("EMULATOR_BACKEND").empty()) {
        // Keep existing working choices as explicit exceptions during policy migration.
        config_.set("EMULATOR_BACKEND", "retroarch");
        if (config_.get("EMULATOR_snes").empty() && config_.get("SNES_EMULATOR") == "pocketsnes")
            config_.set("EMULATOR_snes", "od:pocketsnes");
        if (config_.get("EMULATOR_psx").empty()) config_.set("EMULATOR_psx", "app:org.lilsmoke.pcsx4all");
        if (!config_.save()) LOG_WARN("Emulator policy migration could not be persisted");
    }

    if (config_.get("EMULATOR_psx") == "od:pcsx4all") {
        config_.set("EMULATOR_psx", "app:org.lilsmoke.pcsx4all");
        if (!config_.save()) LOG_WARN("PCSX4ALL application migration could not be persisted");
    }

    // --- Init renderer ---
    int displayWidth = config_.getInt("DISPLAY_WIDTH", 640);
    int displayHeight = config_.getInt("DISPLAY_HEIGHT", 480);
    if (displayWidth < 320 || displayWidth > 1920) displayWidth = 640;
    if (displayHeight < 240 || displayHeight > 1080) displayHeight = 480;
    const char *hcfb = std::getenv("LILSMOKE_HCFB");
    const bool balanced = hcfb && std::string(hcfb) == "1" &&
        config_.get("UI_RENDER_MODE", "balanced") != "native";
    const UiResolution uiSize = softwareUiResolution(displayWidth, displayHeight, balanced);
    LOG_INFO("UI: rendering=%s requested=%dx%d canvas=%dx%d; game modes unchanged",
        balanced ? "balanced" : "native", displayWidth, displayHeight, uiSize.width, uiSize.height);
    displayWidth = uiSize.width; displayHeight = uiSize.height;
    renderer_.setTextureCacheLimitMB(config_.getInt("ASSET_CACHE_MB", 8));

    std::string screenWidth = std::to_string(displayWidth);
    std::string screenHeight = std::to_string(displayHeight);
    setenv("LILSMOKE_SCREEN_WIDTH", screenWidth.c_str(), 1);
    setenv("LILSMOKE_SCREEN_HEIGHT", screenHeight.c_str(), 1);
    setenv("LILSMOKE_DRAW_CLOCK",
           config_.getBool("SHOW_CLOCK", false) ? "1" : "0", 1);
    const std::string batteryMode = config_.get("SHOW_BATTERY", "text");
    setenv("LILSMOKE_SHOW_BATTERY", batteryMode.c_str(), 1);
    const std::string batteryOverride =
        config_.get("BATTERY_PERCENT_OVERRIDE", "");
    if (batteryOverride.empty()) unsetenv("LILSMOKE_BATTERY_PERCENT");
    else setenv("LILSMOKE_BATTERY_PERCENT", batteryOverride.c_str(), 1);

    reportLoadingProgress(80, "Starting display");
    if (!renderer_.init(displayWidth, displayHeight)) {
        LOG_ERROR("Renderer init failed — aborting");
        return 1;
    }

    // --- Init font ---
    reportLoadingProgress(85, "Loading fonts");
    std::string fontPath = config_.get("FONT_PATH",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf");
    if (!font_.load(fontPath, 16)) {
        LOG_ERROR("Font load failed — UI will be unusable");
    }

    // --- Load theme ---
    reportLoadingProgress(88, "Loading theme");
    std::string themeName = config_.get("DEFAULT_THEME", "default");
    loadTheme(themeName);

    // --- Init input ---
    reportLoadingProgress(91, "Starting input");
    int nInput = input_.init(displayWidth, displayHeight);
    applyPresentation();
    input_.setPointerOptions(config_.getBool("POINTER_ENABLED", true),
                             config_.get("POINTER_SPEED", "normal"));
    cursors_.load(renderer_, "/usr/share/lilsmoke/cursors/lilsmoke-cursors.webp");
    SDL_ShowCursor(SDL_DISABLE);
    LOG_INFO("Input: %d device(s) opened", nInput);

    // --- Scan ROMs ---
    reportLoadingProgress(94, "Loading games");
    int nRoms = refreshGames(false);
    reportLoadingProgress(98, "Preparing menu");
    LOG_INFO("ROMScanner: %d ROMs found", nRoms);
    if (nRoms == 0) {
        LOG_WARN("No ROMs found in /storage/roms. UI will only show Settings + Exit.");
    }

    // --- Push root view ---
    {
        FavoritesStore preferences;
        if (preferences.load()) hideEmptySystems_ = preferences.hideEmptySystems();
    }
    LOG_INFO("UI: creating root system view");
    push(makeSystemListView());
    GameHistory history;
    if (history.load() && history.startInRecents()) {
        push(makeRecentGamesView());
    }
    const char *resume = std::getenv("LILSMOKE_EXPLORER_RETURN");
    if (resume && dirExists(resume)) {
        std::string directory = resume;
        push(makeExplorerView(directory));
        LOG_INFO("UI: resumed Explorer %s", directory.c_str());
    }
    unsetenv("LILSMOKE_EXPLORER_RETURN");
    unsetenv("LILSMOKE_EXPLORER_FOCUS");
    LOG_INFO("UI: entering main loop");

    // --- Main loop ---
    bool running = true;
    const char *profileValue = std::getenv("LILSMOKE_UI_PROFILE");
    const bool profile = profileValue && std::string(profileValue) != "0";
    const uint64_t counterFrequency = SDL_GetPerformanceFrequency();
    const double ticksPerMs = static_cast<double>(counterFrequency) / 1000.0;
    FrameActivityProfile frameActivity(profile ? counterFrequency : 0);
    uint64_t sdlTicks = 0, inputTicks = 0, renderTicks = 0, cursorTicks = 0, presentTicks = 0, workTicks = 0;
    unsigned profileFrames = 0, profileEvents = 0, profileMotions = 0;
    unsigned composedFrames = 0, retainedFrames = 0;
    const char *reuseOption = std::getenv("LILSMOKE_UI_IDLE_REUSE");
    const bool idleReuse = !reuseOption || std::string(reuseOption) != "0";
    FramePacer framePacer(SDL_GetTicks());
    BootReadyMarker bootReady;
    while (running) {
        const uint64_t workStart = profile ? SDL_GetPerformanceCounter() : 0;
        // SDL controllers supplement the native evdev / RK707 IPC sources.
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            frameReuse_.invalidate();
            if (e.type == SDL_QUIT) { running = false; break; }
            input_.handleSDLEvent(e);
        }

        // Poll input
        InputButton btn;
        InputEvent evt;
        PointerEvent pointer;
        const uint64_t inputStart = profile ? SDL_GetPerformanceCounter() : 0;
        if (profile) sdlTicks += inputStart - workStart;
        input_.setTextMode(current() && current()->acceptsText());
        auto dispatch = [&](InputButton btn, InputEvent evt, const PointerEvent &pointer,
                            const std::string &text) {
            frameReuse_.invalidate();
            if (btn == InputButton::Power) {
                running = false;
                return false;
            }
            View *v = current();
            if (!v) return true;
            v->invalidateVisual();
            if (profile && pointer.type == PointerEventType::Moved) ++profileMotions;
            if (v->acceptsText() && keyboardOwner_ != v) {
                keyboard_.reset(v->textNumeric());
                keyboardOwner_ = v;
            }
            bool autoBackPop = v->allowAutoBackPop();
            View *next = nullptr;
            const bool pointerBack = pointer.type == PointerEventType::Pressed &&
                                     pointer.button == PointerButton::Secondary;
            if (!text.empty()) v->handleText(text);
            else if (v->acceptsText()) {
                const auto result = pointer.type != PointerEventType::None ? keyboard_.pointer(pointer) : keyboard_.input(btn, evt);
                autoBackPop = false;
                if (result.action == KeyboardAction::Text) v->handleText(result.text);
                else if (result.action == KeyboardAction::Accept) next = v->handleInput(InputButton::Start, InputEvent::Pressed);
                else if (result.action == KeyboardAction::Cancel) next = v->handleInput(InputButton::B, InputEvent::Pressed);
            }
            else if (pointer.type != PointerEventType::None) next = v->handlePointer(pointer);
            else next = v->handleInput(btn, evt);
            if (processLaunchRequested() || scanRequested_) return false;
            if (next) {
                // Push (uncommon path — most views return nullptr and use push() internally)
                push(std::unique_ptr<View>(next));
            }
            // B button on a leaf view = pop
            if (autoBackPop && (pointerBack ||
                (btn == InputButton::B && evt == InputEvent::Pressed))) {
                // The view's handleInput already had a chance to react.
                // If we're still on a non-root view and B was pressed,
                // pop. (Views signal "don't pop" by returning non-null above.)
                if (next == nullptr && current() == v && viewStack_.size() > 1) {
                    // Check if this view actually wants to go back.
                    // For simplicity, B always pops unless it's the root view.
                    pop();
                }
            }
            input_.setTextMode(current() && current()->acceptsText());
            if (!current() || !current()->acceptsText()) keyboardOwner_ = nullptr;
            return true;
        };
        PointerMotionBatch motion;
        int inputEvents = 0;
        // Bound dispatch work so a busy pointer cannot postpone presentation forever.
        while (inputEvents < 256 && input_.poll(btn, evt, pointer)) {
            ++inputEvents;
            if (!input_.pointerState().dragging() && motion.append(pointer)) continue;
            if (motion.pending() && !dispatch(InputButton::None, InputEvent::None, motion.take(), {})) break;
            if (!dispatch(btn, evt, pointer, input_.text())) break;
        }
        if (motion.pending() && running && !processLaunchRequested() && !scanRequested_)
            dispatch(InputButton::None, InputEvent::None, motion.take(), {});
        if (processLaunchRequested()) return 0;
        if (auto *games = dynamic_cast<GridView *>(current()))
            games->updateFastScroll(SDL_GetTicks(), input_.state().r2);
        if (profile) {
            inputTicks += SDL_GetPerformanceCounter() - inputStart;
            profileEvents += static_cast<unsigned>(inputEvents);
        }

        if (scanRequested_) {
            frameReuse_.invalidate();
            scanRequested_ = false;
            if (current()) current()->onLeave();
            viewStack_.clear();
            keyboardOwner_ = nullptr;
            refreshGames(true);
            push(makeSystemListView());
        }

        // Render
        View *v = current();
        if (v) {
            const uint64_t renderStart = profile ? SDL_GetPerformanceCounter() : 0;
            const PointerState &p = input_.pointerState();
            const uint32_t now = SDL_GetTicks();
            const ViewActivity activity = v->frameActivity(now, theme_);
            // Pointer/keyboard overlays stay on the conservative every-frame path.
            const bool overlay = v->acceptsText() || p.visible;
            const bool compose = !idleReuse || frameReuse_.needsComposition(v, activity, now,
                theme_.revision(), renderer_.canReuseFrame(), overlay);
            if (compose) v->render(renderer_, font_, theme_);
            if (compose && v->acceptsText()) {
                if (keyboardOwner_ != v) { keyboard_.reset(v->textNumeric()); keyboardOwner_ = v; }
                keyboard_.render(renderer_, font_, v->textTitle(), v->textValue(), v->textSecret());
            } else keyboardOwner_ = nullptr;
            const uint64_t cursorStart = profile ? SDL_GetPerformanceCounter() : 0;
            if (compose && p.visible)
                cursors_.draw(renderer_, v->cursorAt(p.x, p.y), p.x, p.y,
                              config_.getInt("CURSOR_SIZE", 32));
            if (compose) frameReuse_.composed(v, activity, now, theme_.revision(), overlay);
            const uint64_t presentStart = profile ? SDL_GetPerformanceCounter() : 0;
            renderer_.present();
            bootReady.submitted(renderer_.presentStats().submitted);
            if (profile) {
                composedFrames += compose;
                retainedFrames += !compose;
                renderTicks += cursorStart - renderStart;
                cursorTicks += presentStart - cursorStart;
                const uint64_t workEnd = SDL_GetPerformanceCounter();
                presentTicks += workEnd - presentStart;
                workTicks += workEnd - workStart;
                const auto &presentation = renderer_.presentStats();
                frameActivity.record(workStart, inputStart, workEnd,
                    static_cast<unsigned>(inputEvents), presentation.native,
                    presentation.fallback, presentation.rects);
                if (++profileFrames == 120) {
                    const double divisor = ticksPerMs * profileFrames;
                    LOG_INFO("UI_PROFILE frames=%u events=%u motion_dispatches=%u sdl_event_ms=%.3f input_dispatch_ms=%.3f render_ms=%.3f cursor_ms=%.3f present_ms=%.3f frame_work_ms=%.3f",
                             profileFrames, profileEvents, profileMotions, sdlTicks / divisor,
                             inputTicks / divisor, renderTicks / divisor, cursorTicks / divisor,
                             presentTicks / divisor, workTicks / divisor);
                    const auto activity = frameActivity.summarize();
                    LOG_INFO("UI_FRAME_ACTIVITY rendered=%u interval_samples=%u changed_native=%u unchanged_native=%u unknown=%u no_input_unchanged=%u input_frames=%u input_events=%llu work_over_60hz_budget=%u work_p50_ms=%.3f work_p95_ms=%.3f work_max_ms=%.3f interval_p50_ms=%.3f interval_p95_ms=%.3f interval_max_ms=%.3f dispatch_to_present_p95_ms=%.3f",
                             activity.frames, activity.interval.samples, activity.changedNative,
                             activity.unchangedNative, activity.unknown, activity.noInputUnchanged,
                             activity.inputFrames, static_cast<unsigned long long>(activity.inputEvents),
                             activity.workOver60HzBudget, activity.work.p50 / ticksPerMs,
                             activity.work.p95 / ticksPerMs, activity.work.maximum / ticksPerMs,
                             activity.interval.p50 / ticksPerMs, activity.interval.p95 / ticksPerMs,
                             activity.interval.maximum / ticksPerMs, activity.dispatchToPresent.p95 / ticksPerMs);
                    frameActivity.clearWindow();
                    LOG_INFO("UI_REUSE_PROFILE loops=%u composed=%u retained=%u", profileFrames,
                             composedFrames, retainedFrames);
                    composedFrames = retainedFrames = 0;
                    sdlTicks = inputTicks = renderTicks = cursorTicks = presentTicks = workTicks = 0;
                    profileFrames = profileEvents = profileMotions = 0;
                }
            }
        }

        const uint32_t delay = framePacer.delayAfterFrame(SDL_GetTicks());
        if (delay) SDL_Delay(delay);
    }

    LOG_INFO("LilSmokeUI exiting");
    return 0;
}

}  // namespace lilsmoke

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int result;
    lilsmoke::UiWarmReturn warmReturn;
    std::optional<lilsmoke::ProcessLaunch> launch;
    {
        lilsmoke::App app(warmReturn.warm);
        result = app.run();
        launch = lilsmoke::takeProcessLaunch();
    }
    // SDL_Quit and input/texture destruction must precede execve: an exec
    // does not run destructors or shut down DirectFB's shared input context.
    if (!launch) return result;
    const bool recorded = warmReturn.recordHandoff();
    result = lilsmoke::executeProcessLaunch(std::move(*launch));
    // A failed exec is not an application return.
    if (recorded) unlink(lilsmoke::uiAppHandoff);
    return result;
}
