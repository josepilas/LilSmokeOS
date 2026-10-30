// Bounded real-SDL/DirectFB regression. Input is an in-process fixture, not QMP keys.
#include "Config.h"
#include "Cursor.h"
#include "Font.h"
#include "Input.h"
#include "ProcessLaunch.h"
#include "Renderer.h"
#include "SettingsView.h"
#include "SystemView.h"
#include "Theme.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

using namespace lilsmoke;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void stage(const char *name) { std::printf("QA_STAGE=%s\n", name); }

unsigned taskCount() {
    DIR *tasks = opendir("/proc/self/task");
    require(tasks != nullptr, "cannot inspect UI thread cleanup");
    unsigned count = 0;
    while (const dirent *entry = readdir(tasks)) {
        if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') ++count;
    }
    closedir(tasks);
    return count;
}

void runtimeLibraries() {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    std::vector<std::string> paths;
    while (std::getline(maps, line)) {
        const auto start = line.find('/');
        if (start == std::string::npos) continue;
        std::string path = line.substr(start);
        if (path.find("libSDL2") == std::string::npos &&
            path.find("libdirect") == std::string::npos &&
            path.find("libfusion") == std::string::npos) continue;
        if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
            paths.push_back(path);
            std::printf("QA_DSO=%s\n", path.c_str());
        }
    }
    for (const char *name : {"libSDL2-", "libSDL2_image", "libSDL2_ttf",
                             "libdirectfb-", "libfusion-", "libdirect-"}) {
        require(std::any_of(paths.begin(), paths.end(), [name](const std::string &p) {
            return p.find(name) != std::string::npos;
        }), "real SDL2/DirectFB dependency missing from process maps");
    }
}

// Read the actual software renderer, including decoded theme assets and cursor.
void capture(Renderer &renderer, const std::string &path) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, renderer.width(), renderer.height(), 32, SDL_PIXELFORMAT_ARGB8888);
    require(surface != nullptr, "capture surface allocation failed");
    const int result = SDL_RenderReadPixels(static_cast<SDL_Renderer *>(renderer.sdlRenderer()),
        nullptr, surface->format->format, surface->pixels, surface->pitch);
    if (result != 0) { SDL_FreeSurface(surface); require(false, "render readback failed"); }
    unsigned different = 0;
    uint32_t hash = 2166136261U;
    const auto first = *static_cast<const uint32_t *>(surface->pixels);
    for (int y = 0; y < surface->h; ++y) {
        const auto *row = reinterpret_cast<const uint32_t *>(
            static_cast<const unsigned char *>(surface->pixels) + y * surface->pitch);
        for (int x = 0; x < surface->w; ++x) {
            different += row[x] != first;
            hash = (hash ^ row[x]) * 16777619U;
        }
    }
    const bool saved = path.empty() || SDL_SaveBMP(surface, path.c_str()) == 0;
    SDL_FreeSurface(surface);
    require(different > 100, "blank UI frame");
    require(saved, "capture write failed");
    std::printf("QA_PIXELS hash=%08x nonuniform=%u\n", hash, different);
}

class FixtureApp {
public:
    // Declaration order mirrors App: views/cursors/input/font die before Renderer/SDL.
    Config config;
    Renderer renderer;
    Font font;
    Theme theme;
    std::string themeDir;
    Input input;
    CursorAtlas cursors;
    std::vector<std::unique_ptr<View>> views;
    unsigned powerCallbacks = 0;
    unsigned rootReturns = 0;

    void push(std::unique_ptr<View> view) {
        if (!views.empty()) views.back()->onLeave();
        views.push_back(std::move(view));
        views.back()->onEnter();
    }

    void press(InputButton button) {
        require(!views.empty(), "input without active view");
        View *next = views.back()->handleInput(button, InputEvent::Pressed);
        require(next == nullptr, "unexpected returned view");
    }

    void render(unsigned frames, const std::string &snapshot = "") {
        for (unsigned i = 0; i < frames; ++i) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                require(event.type != SDL_QUIT, "unexpected SDL quit");
                input.handleSDLEvent(event);
            }
            InputButton button;
            InputEvent action;
            PointerEvent pointer;
            unsigned drained = 0;
            while (input.poll(button, action, pointer)) {
                require(++drained < 256, "unbounded input queue");
            }
            views.back()->render(renderer, font, theme);
            cursors.draw(renderer, views.back()->cursorAt(48, 64), 48, 64, 32);
            if (i + 1 == frames) capture(renderer, snapshot);
            renderer.present();
            SDL_Delay(10);
        }
    }

    int run(const char *directory, const char *fontPath, unsigned frames,
            const std::string &snapshotPrefix) {
        require(config.load("/persist/config/lilsmoke.conf"), "config load failed");
        themeDir = directory;
        config.set("DEFAULT_THEME", themeDir.substr(themeDir.find_last_of('/') + 1));
        renderer.setTextureCacheLimitMB(8);
        require(renderer.init(856, 480), "Renderer init failed");
        require(SDL_GetCurrentVideoDriver() &&
                !std::strcmp(SDL_GetCurrentVideoDriver(), "directfb"), "not SDL DirectFB");
        SDL_RendererInfo info{};
        require(SDL_GetRendererInfo(static_cast<SDL_Renderer *>(renderer.sdlRenderer()), &info) == 0 &&
                (info.flags & SDL_RENDERER_SOFTWARE) && !(info.flags & SDL_RENDERER_ACCELERATED),
                "not ordinary SDL software rendering");
        stage("renderer-ready");
        runtimeLibraries();
        require(font.load(fontPath, 16), "font load failed");
        require(theme.loadSet(themeDir), "theme load failed");
        require(theme.loaded(), "theme unexpectedly unloaded");
        input.init(856, 480);
        input.setPointerOptions(true, "normal");
        require(cursors.load(renderer, "/usr/share/lilsmoke/cursors/lilsmoke-cursors.webp"),
                "real cursor atlas load failed");
        SDL_ShowCursor(SDL_DISABLE);
        auto system = std::make_unique<SystemView>(
            std::vector<SystemEntry>{{"nes", "NES / Famicom", 2},
                                     {"snes", "SNES / Super Famicom", 1},
                                     {"__settings", "Settings", 0}, {"__exit", "Exit", 0}},
            [this](const SystemEntry &entry) -> View * {
                require(entry.id == "__settings", "selected something other than Settings");
                auto settings = std::make_unique<SettingsView>(config);
                settings->setOnBack([this]() -> View * { ++rootReturns; return nullptr; });
                settings->setOnPowerOff([this]() {
                    ++powerCallbacks;
                    require(config.save(), "power callback config save failed");
                    requestProcessLaunch({"/usr/lib/lilsmoke/lilsmoke-power",
                                          {"lilsmoke-power", "poweroff"}});
                });
                push(std::move(settings));
                return nullptr;
            }, &themeDir);
        push(std::move(system));
        render(frames, snapshotPrefix.empty() ? "" : snapshotPrefix + "-system.bmp");
        stage("theme-frames");
        press(InputButton::Right);
        render(frames);
        press(InputButton::Right);
        render(frames);
        press(InputButton::A);
        require(views.size() == 2, "Settings did not open from SystemView");
        stage("settings-open");
        render(1);
        press(InputButton::A);
        require(!views.back()->allowAutoBackPop(), "Power Off dialog did not open");
        render(1);
        press(InputButton::A); // Default focus is Cancel.
        require(!processLaunchRequested() && powerCallbacks == 0, "Cancel requested shutdown");
        require(access("/run/lilsmoke-poweroff-request", F_OK) != 0, "Cancel published helper marker");
        require(views.back()->allowAutoBackPop(), "Cancel did not close dialog");
        press(InputButton::A);
        press(InputButton::Right);
        press(InputButton::Left);
        render(1);
        press(InputButton::A); // Explicit navigation back to Cancel.
        require(!processLaunchRequested() && powerCallbacks == 0 && rootReturns == 0 && views.size() == 2,
                "navigated Cancel launched helper or returned to root");
        require(access("/run/lilsmoke-poweroff-request", F_OK) != 0,
                "navigated Cancel published helper marker");
        require(views.back()->allowAutoBackPop(), "navigated Cancel did not close dialog");
        press(InputButton::A);
        press(InputButton::B); // B must not pop Settings while confirmation is open.
        require(!processLaunchRequested() && rootReturns == 0 && views.size() == 2,
                "B confirmation cancellation returned to root");
        require(access("/run/lilsmoke-poweroff-request", F_OK) != 0, "B cancel published helper marker");
        std::puts("QA_CANCEL default=1 navigated=1 back=1 no_helper_marker=1 no_root_return=1");
        stage("cancel-checked");
        press(InputButton::A);
        press(InputButton::Right);
        render(1, snapshotPrefix.empty() ? "" : snapshotPrefix + "-confirm.bmp");
        require(!processLaunchRequested(), "shutdown requested before confirming A");
        press(InputButton::A);
        require(processLaunchRequested() && powerCallbacks == 1, "confirmed shutdown not queued once");
        require(rootReturns == 0 && views.size() == 2, "Power Off returned to root");
        require(access("/run/lilsmoke-poweroff-request", F_OK) != 0,
                "shutdown helper ran before UI destruction");
        stage("launch-requested");
        return 0; // Like App::run: stop rendering once a process launch is pending.
    }
};
} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    try {
#ifndef __mips__
        require(false, "this integration harness must run on MIPS");
#endif
        require(argc == 6, "usage: themed-power THEME_DIR FONT FRAMES SNAPSHOT_PREFIX MODE");
        const char *supervisor = std::getenv("LILSMOKE_STOCK_SUPERVISOR");
        require(supervisor && !std::strcmp(supervisor, "1"), "stock request-only supervisor required");
        const char *hcfb = std::getenv("LILSMOKE_HCFB");
        require(!hcfb || std::strcmp(hcfb, "1"), "physical HC framebuffer must be disabled in VM");
        const std::string mode = argv[5];
        require(mode == "poweroff" || mode == "refused" || mode == "exec-failure", "invalid mode");
        char *end = nullptr;
        const unsigned long frames = std::strtoul(argv[3], &end, 10);
        require(end && !*end && frames >= 1 && frames <= 12, "invalid frame bound");
        require(!processLaunchRequested(), "stale process launch");
        require(access("/run/lilsmoke-poweroff-request", F_OK) != 0, "stale shutdown request");
        std::printf("QA_BEGIN theme=%s mode=%s\n", argv[1], argv[5]);
        std::optional<ProcessLaunch> launch;
        int result;
        {
            FixtureApp app;
            result = app.run(argv[1], argv[2], static_cast<unsigned>(frames), argv[4]);
            launch = takeProcessLaunch();
            require(launch.has_value() && !processLaunchRequested(), "takeProcessLaunch failed");
            require(launch->executable == "/usr/lib/lilsmoke/lilsmoke-power" &&
                    launch->arguments == std::vector<std::string>{"lilsmoke-power", "poweroff"},
                    "incorrect production Power Off request");
            stage("launch-taken");
        }
        // The original regression faults/hangs in this scope's Renderer/SDL teardown.
        require(SDL_WasInit(0) == 0, "SDL survived the UI destructor scope");
        const unsigned tasks = taskCount();
        std::printf("QA_THREADS_AFTER_UI=%u\n", tasks);
        require(tasks == 1, "SDL/DirectFB threads survived UI teardown");
        stage("ui-destroyed");
        require(access("/run/lilsmoke-poweroff-request", F_OK) != 0, "request before exec");
        if (mode == "exec-failure") launch->executable = "/run/qa-missing-power-helper";
        stage("execute-launch");
        result = launch ? executeProcessLaunch(std::move(*launch)) : result;
        if (mode == "exec-failure") {
            require(result == 1, "exec failure did not propagate");
            require(access("/run/lilsmoke-poweroff-request", F_OK) != 0, "failed exec published request");
            stage("exec-failure-propagated");
        } else {
            require(false, "executeProcessLaunch unexpectedly returned instead of replacing process");
        }
        return result;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "QA_FAILURE=%s SDL=%s\n", error.what(), SDL_GetError());
        return 90;
    }
}
