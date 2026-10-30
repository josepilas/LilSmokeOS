#include "native-menu.h"
#include "Input.h"
#include "EmulatorRegistry.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <filesystem>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace {
constexpr size_t selectionLimit = 4096;
std::string runNativeMenu() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) || TTF_Init()) {
        std::fprintf(stderr, "PCSX native menu initialization: %s\n", SDL_GetError());
        _exit(10);
    }
    int width = 640, height = 480;
    SDL_DisplayMode display{};
    if (SDL_GetCurrentDisplayMode(0, &display) == 0) { width = display.w; height = display.h; }
    if (const char *w = std::getenv("LILSMOKE_SCREEN_WIDTH")) width = std::atoi(w);
    if (const char *h = std::getenv("LILSMOKE_SCREEN_HEIGHT")) height = std::atoi(h);
    const char *native = std::getenv("LILSMOKE_M8_NATIVE_VIDEO");
    if (native && !std::strcmp(native, "1")) { width = 426; height = 240; }
    width = std::clamp(width, 320, 1920); height = std::clamp(height, 240, 1080);
    SDL_Window *window = SDL_CreateWindow("PCSX4ALL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          width, height, SDL_WINDOW_FULLSCREEN);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    TTF_Font *font = TTF_OpenFont("/usr/share/fonts/dejavu/DejaVuSans.ttf", height < 400 ? 16 : 22);
    if (!renderer || !font) {
        std::fprintf(stderr, "PCSX native menu: %s\n", SDL_GetError());
        if (font) TTF_CloseFont(font);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        TTF_Quit(); _exit(10);
    }
    lilsmoke::Input input;
    SDL_ShowCursor(SDL_DISABLE);
    input.init(width, height);
    fs::path directory = "/storage/roms/psx";
    std::vector<fs::path> entries;
    std::vector<std::string> labels{"Load game", "Quit"};
    int selected = 0;
    bool browser = false, active = true;
    std::string chosen, error;
    const auto refresh = [&]() {
        labels = {".."}; entries.clear(); entries.push_back(directory.parent_path()); selected = 0;
        std::error_code ec;
        std::vector<fs::path> paths;
        for (fs::directory_iterator it(directory, ec), end; !ec && it != end && paths.size() < 4096; it.increment(ec)) {
            if (it->is_symlink(ec) || it->path().filename().string().front() == '.') continue;
            auto extension = it->path().extension().string();
            for (char &c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (it->is_directory(ec) || extension == ".cue" || extension == ".bin" || extension == ".img" ||
                extension == ".iso" || extension == ".pbp" || extension == ".zip" || extension == ".7z" ||
                extension == ".rar" || extension == ".tar" || extension == ".gz" || extension == ".bz2" ||
                extension == ".xz" || extension == ".tgz" || extension == ".txz" || extension == ".tbz2") paths.push_back(it->path());
        }
        std::sort(paths.begin(), paths.end());
        for (const auto &path : paths) { entries.push_back(path); labels.push_back(path.filename().string()); }
    };
    const auto text = [&](const std::string &value, int x, int y, SDL_Color color) {
        SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value.c_str(), color);
        if (!surface) return;
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_Rect dst{x, y, surface->w, surface->h}; SDL_FreeSurface(surface);
        SDL_Rect clip{12, y, width - 24, 32}; SDL_RenderSetClipRect(renderer, &clip);
        if (texture) { SDL_RenderCopy(renderer, texture, nullptr, &dst); SDL_DestroyTexture(texture); }
        SDL_RenderSetClipRect(renderer, nullptr);
    };
    std::fprintf(stderr, "PCSX_OWN_MENU_ENTER\n");
    while (active) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) active = false;
        lilsmoke::InputButton button; lilsmoke::InputEvent action; lilsmoke::PointerEvent pointer;
        while (input.poll(button, action, pointer)) {
            if (action != lilsmoke::InputEvent::Pressed && action != lilsmoke::InputEvent::Repeat) continue;
            if (button == lilsmoke::InputButton::Up) selected = std::max(0, selected - 1);
            if (button == lilsmoke::InputButton::Down) selected = std::min(static_cast<int>(labels.size()) - 1, selected + 1);
            if (button == lilsmoke::InputButton::Home || button == lilsmoke::InputButton::Power) active = false;
            if (button == lilsmoke::InputButton::B && action == lilsmoke::InputEvent::Pressed) {
                if (browser) { browser = false; labels = {"Load game", "Quit"}; selected = 0; }
                else active = false;
            }
            if ((button != lilsmoke::InputButton::A && button != lilsmoke::InputButton::Start) || action != lilsmoke::InputEvent::Pressed) continue;
            if (!browser) {
                if (selected == 1) active = false;
                else { browser = true; refresh(); }
            } else {
                std::error_code ec;
                const auto path = entries[selected];
                if (fs::is_directory(path, ec)) { directory = path; refresh(); }
                else {
                    const char *resolver = std::getenv("LILSMOKE_ROM_RESOLVER");
                    chosen = lilsmoke::readEmulatorHelper(resolver ? resolver : "/usr/lib/lilsmoke/lilsmoke-rom",
                                                          {"psx", path.string()}, 4096, 300, false);
                    if (!chosen.empty() && chosen.back() == '\n') chosen.pop_back();
                    if (chosen.empty()) error = "Could not load game data";
                    else { chosen = path.string(); active = false; }
                }
            }
        }
        SDL_SetRenderDrawColor(renderer, 24, 28, 32, 255); SDL_RenderClear(renderer);
        text("PCSX4ALL", 16, 12, {255, 255, 255, 255});
        const int rows = std::max(1, (height - 100) / 36), first = std::max(0, selected - rows + 1);
        for (int i = first; i < std::min(static_cast<int>(labels.size()), first + rows); ++i) {
            const int y = 58 + (i - first) * 36;
            if (i == selected) { SDL_SetRenderDrawColor(renderer, 47, 103, 74, 255); SDL_Rect row{12, y, width - 24, 34}; SDL_RenderFillRect(renderer, &row); }
            text(labels[i], 20, y + 4, {235, 239, 237, 255});
        }
        if (!error.empty()) text(error, 16, height - 34, {255, 170, 150, 255});
        SDL_RenderPresent(renderer); SDL_Delay(16);
    }
    TTF_CloseFont(font); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); TTF_Quit();
    return chosen;
}
}  // namespace

std::string pcsxNativeMenu() {
    int resultPipe[2];
    if (pipe2(resultPipe, O_CLOEXEC)) {
        std::perror("PCSX native menu result pipe"); std::exit(10);
    }
    std::fflush(nullptr);
    const pid_t parent = getpid(), worker = fork();
    if (worker < 0) {
        std::perror("PCSX native menu fork");
        close(resultPipe[0]); close(resultPipe[1]); std::exit(10);
    }
    if (!worker) {
        close(resultPipe[0]);
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent) _exit(10);
        try {
            const auto chosen = runNativeMenu();
            if (chosen.size() > selectionLimit) _exit(10);
            size_t sent = 0;
            while (sent < chosen.size()) {
                const ssize_t count = write(resultPipe[1], chosen.data() + sent, chosen.size() - sent);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) _exit(10);
                sent += static_cast<size_t>(count);
            }
            close(resultPipe[1]);
            std::fprintf(stderr, "PCSX_OWN_MENU_WORKER_STOP selected=%d\n", !chosen.empty());
            std::fflush(nullptr);
            // DirectFB's Fusion thread crashes in musl during SDL_Quit. Keep
            // SDL in this worker; process exit releases its remaining context.
            _exit(0);
        } catch (const std::exception &error) {
            std::fprintf(stderr, "PCSX native menu worker: %s\n", error.what());
            _exit(10);
        }
    }
    close(resultPipe[1]);
    std::string chosen;
    char buffer[512];
    bool valid = true;
    for (;;) {
        const ssize_t count = read(resultPipe[0], buffer, sizeof buffer);
        if (count < 0 && errno == EINTR) continue;
        if (!count) break;
        if (count < 0) { valid = false; break; }
        if (chosen.size() + static_cast<size_t>(count) > selectionLimit) valid = false;
        if (valid) chosen.append(buffer, static_cast<size_t>(count));
    }
    close(resultPipe[0]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(worker, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited != worker || !WIFEXITED(status) || WEXITSTATUS(status) || !valid ||
        chosen.find('\0') != std::string::npos) {
        std::fprintf(stderr, "PCSX_OWN_MENU_WORKER_ERROR status=%d signal=%d result_valid=%d\n",
                     status, waited == worker && WIFSIGNALED(status) ? WTERMSIG(status) : 0, valid);
        std::exit(waited == worker && WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 10);
    }
    std::fprintf(stderr, "PCSX_OWN_MENU_EXIT selected=%d worker_exit=0\n", !chosen.empty());
    return chosen;
}
