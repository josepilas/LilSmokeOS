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
#include "View.h"

#include <SDL2/SDL.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string g_storageRoot;
std::string g_retroarchExe;
std::string g_coreDir;
std::string g_sevenZipExe;
std::string g_fontPath;
std::string g_configPath;
std::string g_logDir;

std::string slash(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool endsWith(const std::string &s, const std::string &suffix) {
    if (s.size() < suffix.size()) return false;
    return lower(s).compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string quoteArg(const std::string &arg) {
    std::string out = "\"";
    int backslashes = 0;
    for (char ch : arg) {
        if (ch == '\\') {
            backslashes++;
            continue;
        }
        if (ch == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(ch);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

int runProcess(const std::string &exe, const std::vector<std::string> &args,
               const std::string &workingDir = "") {
    std::string cmd = quoteArg(exe);
    for (const auto &arg : args) {
        cmd.push_back(' ');
        cmd += quoteArg(arg);
    }

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::vector<char> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back('\0');
    std::vector<char> mutableWorkingDir(workingDir.begin(), workingDir.end());
    mutableWorkingDir.push_back('\0');
    const char *cwd = workingDir.empty() ? nullptr : mutableWorkingDir.data();

    BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                             0, nullptr, cwd, &si, &pi);
    if (!ok) return (int)GetLastError();

    SetProcessAffinityMask(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

std::string safeArchiveName(const fs::path &path) {
    std::string name = path.filename().string();
    for (char &ch : name) {
        bool ok = std::isalnum((unsigned char)ch) || ch == '.' || ch == '_' || ch == '-';
        if (!ok) ch = '_';
    }
    return name;
}

std::vector<std::string> romExtsFor(const std::string &system) {
    if (system == "snes" || system == "sfc") return {".sfc", ".smc", ".fig", ".bs"};
    if (system == "nes") return {".nes", ".unif", ".unf", ".fds"};
    if (system == "gb") return {".gb"};
    if (system == "gbc") return {".gbc"};
    if (system == "gba") return {".gba"};
    if (system == "md") return {".md", ".gen", ".smd", ".bin"};
    return {};
}

std::string findExtractedRom(const std::string &system, const fs::path &dir) {
    std::error_code ec;
    auto exts = romExtsFor(system);
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string p = slash(it->path().string());
        std::string lp = lower(p);
        for (const auto &ext : exts) {
            if (endsWith(lp, ext)) return p;
        }
    }
    return "";
}

std::string extract7zRom(const std::string &system, const std::string &archive, std::string &error) {
    fs::path cacheDir = fs::path(g_storageRoot) / ".cache" / "lilsmoke" / "archives" /
                        system / safeArchiveName(archive);
    fs::create_directories(cacheDir.parent_path());

    std::string existing = findExtractedRom(system, cacheDir);
    if (!existing.empty()) return existing;

    fs::path tmp = cacheDir;
    tmp += ".tmp";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (ec) {
        error = "Cannot create archive cache: " + tmp.string();
        return "";
    }

    int rc = runProcess(g_sevenZipExe, {"x", "-y", "-o" + tmp.string(), archive});
    if (rc != 0) {
        error = "7z extraction failed with code " + std::to_string(rc);
        fs::remove_all(tmp, ec);
        return "";
    }

    fs::remove_all(cacheDir, ec);
    fs::rename(tmp, cacheDir, ec);
    if (ec) {
        error = "Cannot move extracted archive into cache: " + cacheDir.string();
        return "";
    }

    std::string rom = findExtractedRom(system, cacheDir);
    if (rom.empty()) error = "No supported ROM found inside archive";
    return rom;
}

void writeRetroArchConfig(const fs::path &saveDir, const fs::path &stateDir) {
    fs::create_directories(fs::path(g_configPath).parent_path());
    fs::create_directories(saveDir);
    fs::create_directories(stateDir);

    std::ofstream out(g_configPath, std::ios::binary);
    out
        << "video_driver = \"sdl2\"\n"
        << "video_threaded = \"false\"\n"
        << "video_fullscreen = \"false\"\n"
        << "video_windowed_fullscreen = \"false\"\n"
        << "video_window_width = \"640\"\n"
        << "video_window_height = \"480\"\n"
        << "video_scale_integer = \"true\"\n"
        << "video_smooth = \"false\"\n"
        << "video_vsync = \"false\"\n"
        << "audio_enable = \"false\"\n"
        << "input_driver = \"sdl2\"\n"
        << "joypad_driver = \"sdl2\"\n"
        << "keyboard_gamepad_enable = \"true\"\n"
        << "pause_nonactive = \"false\"\n"
        << "config_save_on_exit = \"false\"\n"
        << "savefile_directory = \"" << slash(saveDir.string()) << "\"\n"
        << "savestate_directory = \"" << slash(stateDir.string()) << "\"\n"
        << "input_exit_emulator = \"escape\"\n"
        << "input_player1_start = \"enter\"\n"
        << "input_player1_select = \"rshift\"\n"
        << "input_player1_up = \"up\"\n"
        << "input_player1_down = \"down\"\n"
        << "input_player1_left = \"left\"\n"
        << "input_player1_right = \"right\"\n"
        << "input_player1_a = \"x\"\n"
        << "input_player1_b = \"z\"\n"
        << "input_player1_x = \"s\"\n"
        << "input_player1_y = \"a\"\n";
}

std::string coreForSystem(const std::string &system) {
    if (system == "snes" || system == "sfc") {
        return slash((fs::path(g_coreDir) / "snes9x2005_libretro.dll").string());
    }
    return "";
}

std::string displayName(const std::string &system) {
    if (system == "snes") return "SNES / Super Famicom";
    if (system == "nes") return "NES / Famicom";
    if (system == "gb") return "Game Boy";
    if (system == "gbc") return "Game Boy Color";
    if (system == "gba") return "Game Boy Advance";
    if (system == "md") return "Genesis / Mega Drive";
    if (system == "portsmoke") return "PortSmoke";
    return system;
}

bool isRetroArchSystem(const std::string &system) {
    return system == "nes" || system == "snes" || system == "gb" || system == "gbc" ||
           system == "gba" || system == "md" || system == "sms" || system == "gg" ||
           system == "pce" || system == "arcade" || system == "lynx";
}

class HostRetroArchMenuView : public lilsmoke::View {
public:
    void onEnter() override {
        if (launched_) return;
        launched_ = true;
        run();
    }

    lilsmoke::View *handleInput(lilsmoke::InputButton, lilsmoke::InputEvent) override {
        return nullptr;
    }

    void render(lilsmoke::Renderer &r, lilsmoke::Font &font, const lilsmoke::Theme &) override {
        r.clear(ok_ ? 0x1F3D2BFF : 0x2B2D31FF);
        font.render(r, "RetroArch", 16, r.height() / 2 - 32, 0xD9C9A3FF);
        font.render(r, message_, 16, r.height() / 2 - 8, ok_ ? 0xD9C9A3CC : 0xFF9090FF);
        font.render(r, "Press B/Escape to go back", 16, r.height() - 32, 0xD9C9A399);
    }

private:
    bool launched_ = false;
    bool ok_ = false;
    std::string message_ = "Starting RetroArch...";

    void run() {
        fs::path saveDir = fs::path(g_storageRoot) / "saves";
        fs::path stateDir = fs::path(g_storageRoot) / "states";
        fs::path logPath = fs::path(g_logDir) / "retroarch-last.log";
        fs::create_directories(logPath.parent_path());
        std::error_code ec;
        fs::remove(logPath, ec);
        writeRetroArchConfig(saveDir, stateDir);

        SetEnvironmentVariableA("SDL_VIDEODRIVER", "windows");
        SetEnvironmentVariableA("SDL_AUDIODRIVER", "directsound");
        SetEnvironmentVariableA("DISPLAY", nullptr);

        int rc = runProcess(g_retroarchExe, {
            "-v",
            "--log-file=" + slash(logPath.string()),
            "--config", g_configPath,
        }, slash(fs::path(g_retroarchExe).parent_path().string()));
        ok_ = (rc == 0);
        message_ = ok_ ? "RetroArch exited cleanly"
                       : "RetroArch exited with code " + std::to_string(rc) +
                             ". See logs/retroarch-last.log";
    }
};

class HostLauncherView : public lilsmoke::View {
public:
    explicit HostLauncherView(lilsmoke::ROM rom) : rom_(std::move(rom)) {}

    void onEnter() override {
        if (launched_) return;
        launched_ = true;
        run();
    }

    lilsmoke::View *handleInput(lilsmoke::InputButton, lilsmoke::InputEvent) override {
        return nullptr;
    }

    void render(lilsmoke::Renderer &r, lilsmoke::Font &font, const lilsmoke::Theme &) override {
        r.clear(ok_ ? 0x1F3D2BFF : 0x2B2D31FF);
        font.render(r, title_, 16, r.height() / 2 - 32, 0xD9C9A3FF);
        font.render(r, message_, 16, r.height() / 2 - 8, ok_ ? 0xD9C9A3CC : 0xFF9090FF);
        font.render(r, "Press B/Escape to go back", 16, r.height() - 32, 0xD9C9A399);
    }

private:
    lilsmoke::ROM rom_;
    bool launched_ = false;
    bool ok_ = false;
    std::string title_ = "Launching " + rom_.name;
    std::string message_ = "Preparing emulator...";

    void run() {
        std::string content = slash(rom_.path);
        if (endsWith(content, ".7z")) {
            std::string error;
            content = extract7zRom(rom_.system, content, error);
            if (content.empty()) {
                message_ = error;
                return;
            }
        }

        std::string core = coreForSystem(rom_.system);
        if (core.empty() || !fs::exists(core)) {
            message_ = "No bundled host core for " + rom_.system;
            return;
        }
        if (!fs::exists(g_retroarchExe)) {
            message_ = "RetroArch missing: " + g_retroarchExe;
            return;
        }

        fs::path saveDir = fs::path(g_storageRoot) / "saves" / rom_.system;
        fs::path stateDir = fs::path(g_storageRoot) / "states" / rom_.system;
        fs::path logPath = fs::path(g_logDir) / "retroarch-last.log";
        fs::create_directories(logPath.parent_path());
        std::error_code ec;
        fs::remove(logPath, ec);
        writeRetroArchConfig(saveDir, stateDir);

        SetEnvironmentVariableA("SDL_VIDEODRIVER", "windows");
        SetEnvironmentVariableA("SDL_AUDIODRIVER", "directsound");
        SetEnvironmentVariableA("DISPLAY", nullptr);

        int rc = runProcess(g_retroarchExe, {
            "-v",
            "--log-file=" + slash(logPath.string()),
            "-L", core,
            "--config", g_configPath,
            content,
        }, slash(fs::path(g_retroarchExe).parent_path().string()));
        ok_ = (rc == 0);
        message_ = ok_ ? "Game exited cleanly"
                       : "RetroArch exited with code " + std::to_string(rc) +
                             ". See logs/retroarch-last.log";
    }
};

class EmulatorApp {
public:
    int run(int argc, char **argv) {
        parseArgs(argc, argv);
        prepareStorage();

        SDL_SetMainReady();
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SetEnvironmentVariableA("SDL_VIDEODRIVER", "windows");

        if (!renderer_.init(640, 480)) return 2;
        if (!font_.load(g_fontPath, 16)) return 3;

        rescan();
        push(makeRoot());

        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN) {
                    auto btn = mapKey(e.key.keysym.sym);
                    if (btn == lilsmoke::InputButton::Power) {
                        running = false;
                    } else if (btn != lilsmoke::InputButton::None) {
                        dispatch(btn, e.key.repeat ? lilsmoke::InputEvent::Repeat
                                                   : lilsmoke::InputEvent::Pressed);
                    }
                }
            }

            if (current()) {
                current()->render(renderer_, font_, theme_);
                renderer_.present();
            }
            SDL_Delay(16);
        }
        return 0;
    }

private:
    lilsmoke::Renderer renderer_;
    lilsmoke::Font font_;
    lilsmoke::Theme theme_;
    lilsmoke::ROMScanner scanner_;
    std::vector<std::unique_ptr<lilsmoke::View>> stack_;

    lilsmoke::View *current() { return stack_.empty() ? nullptr : stack_.back().get(); }

    void push(std::unique_ptr<lilsmoke::View> view) {
        if (current()) current()->onLeave();
        stack_.push_back(std::move(view));
        current()->onEnter();
    }

    void pop() {
        if (stack_.size() <= 1) return;
        current()->onLeave();
        stack_.pop_back();
        current()->onEnter();
    }

    void parseArgs(int argc, char **argv) {
        fs::path exeDir = fs::absolute(fs::path(argv[0])).parent_path();
        g_storageRoot = slash((exeDir.parent_path() / "storage").string());
        g_coreDir = slash((exeDir / "cores").string());
        g_retroarchExe = slash((exeDir / "retroarch" / "RetroArch-Win64" / "retroarch.exe").string());
        g_sevenZipExe = slash((exeDir / "7zip" / "7z.exe").string());
        g_fontPath = "C:/Windows/Fonts/arial.ttf";
        g_configPath = slash((exeDir / "retroarch-lilsmoke.cfg").string());
        g_logDir = slash((exeDir.parent_path() / "logs").string());

        for (int i = 1; i + 1 < argc; i += 2) {
            std::string k = argv[i];
            std::string v = slash(argv[i + 1]);
            if (k == "--storage") g_storageRoot = v;
            else if (k == "--retroarch") g_retroarchExe = v;
            else if (k == "--core-dir") g_coreDir = v;
            else if (k == "--7z") g_sevenZipExe = v;
            else if (k == "--font") g_fontPath = v;
            else if (k == "--config") g_configPath = v;
            else if (k == "--logs") g_logDir = v;
        }
    }

    void prepareStorage() {
        for (const char *dir : {"roms/snes", "saves", "states", "themes", "portsmoke/ports", "portsmoke/engines"}) {
            fs::create_directories(fs::path(g_storageRoot) / dir);
        }
    }

    void rescan() {
        scanner_.scan(slash((fs::path(g_storageRoot) / "roms").string()));
    }

    std::unique_ptr<lilsmoke::View> makeRoot() {
        auto systems = scanner_.systems();
        std::vector<std::string> items = {"Settings", "LilSmoke Explorer", "RetroArch"};
        for (const auto &system : systems) items.push_back(displayName(system));
        items.push_back("Exit");

        return std::make_unique<lilsmoke::ListView>(
            items,
            [this](int idx, const std::string &) -> lilsmoke::View * {
                if (idx == 0) {
                    push(std::make_unique<lilsmoke::SettingsView>(config_));
                    return nullptr;
                }
                if (idx == 1) {
                    auto fv = std::make_unique<lilsmoke::FileExplorerView>(g_storageRoot);
                    fv->setOnBack([this]() -> lilsmoke::View * { pop(); return nullptr; });
                    push(std::move(fv));
                    return nullptr;
                }
                if (idx == 2) {
                    push(makeRetroArchApp());
                    return nullptr;
                }
                auto systems = scanner_.systems();
                if (idx == (int)systems.size() + 3) {
                    std::exit(0);
                }
                int sysIdx = idx - 3;
                if (sysIdx >= 0 && sysIdx < (int)systems.size()) {
                    push(makeGameList(systems[sysIdx]));
                }
                return nullptr;
            },
            "LilSmokeOS Emulator");
    }

    std::unique_ptr<lilsmoke::View> makeRetroArchApp() {
        std::vector<std::string> retroSystems;
        for (const auto &system : scanner_.systems()) {
            if (isRetroArchSystem(system)) retroSystems.push_back(system);
        }

        std::vector<std::string> items = {"Open RetroArch Menu"};
        for (const auto &system : retroSystems) items.push_back(displayName(system));
        if (retroSystems.empty()) items.push_back("No RetroArch games found");

        return std::make_unique<lilsmoke::ListView>(
            std::move(items),
            [this, retroSystems](int idx, const std::string &) -> lilsmoke::View * {
                if (idx == 0) {
                    push(std::make_unique<HostRetroArchMenuView>());
                    return nullptr;
                }
                int sysIdx = idx - 1;
                if (sysIdx >= 0 && sysIdx < (int)retroSystems.size()) {
                    push(makeGameList(retroSystems[sysIdx]));
                }
                return nullptr;
            },
            "RetroArch");
    }

    std::unique_ptr<lilsmoke::View> makeGameList(const std::string &system) {
        auto roms = scanner_.bySystem(system);
        std::vector<const lilsmoke::ROM *> ptrs;
        for (auto *rom : roms) ptrs.push_back(rom);
        return std::make_unique<lilsmoke::GridView>(
            std::move(ptrs),
            [this](const lilsmoke::ROM *rom) -> lilsmoke::View * {
                if (rom) push(std::make_unique<HostLauncherView>(*rom));
                return nullptr;
            },
            displayName(system));
    }

    lilsmoke::Config config_;

    static lilsmoke::InputButton mapKey(SDL_Keycode key) {
        switch (key) {
            case SDLK_UP: return lilsmoke::InputButton::Up;
            case SDLK_DOWN: return lilsmoke::InputButton::Down;
            case SDLK_LEFT: return lilsmoke::InputButton::Left;
            case SDLK_RIGHT: return lilsmoke::InputButton::Right;
            case SDLK_RETURN:
            case SDLK_z: return lilsmoke::InputButton::A;
            case SDLK_ESCAPE:
            case SDLK_x:
            case SDLK_BACKSPACE: return lilsmoke::InputButton::B;
            case SDLK_a: return lilsmoke::InputButton::X;
            case SDLK_s: return lilsmoke::InputButton::Y;
            case SDLK_RSHIFT: return lilsmoke::InputButton::Select;
            case SDLK_q: return lilsmoke::InputButton::L1;
            case SDLK_w: return lilsmoke::InputButton::R1;
            case SDLK_F12: return lilsmoke::InputButton::Power;
            default: return lilsmoke::InputButton::None;
        }
    }

    void dispatch(lilsmoke::InputButton btn, lilsmoke::InputEvent evt) {
        if (!current()) return;
        bool autoBack = current()->allowAutoBackPop();
        lilsmoke::View *next = current()->handleInput(btn, evt);
        if (next) push(std::unique_ptr<lilsmoke::View>(next));
        if (autoBack && btn == lilsmoke::InputButton::B && evt == lilsmoke::InputEvent::Pressed) {
            pop();
        }
    }
};

}  // namespace

int main(int argc, char **argv) {
    EmulatorApp app;
    return app.run(argc, argv);
}
