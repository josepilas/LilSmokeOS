// Native software frontend for the reconstructed PCSX4ALL core.
#include "rkgame_bridge.h"
#include "Input.h"
#include "native-menu.h"
#include "state-store.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <csignal>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unistd.h>

namespace {
SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;
SDL_Texture *texture = nullptr;
SDL_Surface *output = nullptr, *source = nullptr, *converted = nullptr;
SDL_Rect destination{};
bool surfaceVideo = false;
bool nativeVideo = false, paused = false, menuChord = false;
int menuChoice = 0;
int stateSlot = 0, stateAction = 0;
PcsxStateStore stateStore;
std::string stateMessage;
TTF_Font *menuFont = nullptr;
extern "C" void pcsx_audio_pause(int);
lilsmoke::Input input;
volatile sig_atomic_t stopped = 0;
uint16_t pixels[320 * 240]{};
uint64_t frames = 0, lastReport = 0, reportFrames = 0;
uint64_t started = 0, videoTime = 0, benchFrames = 0;
bool benchGameplay = false;
void signalStop(int) { stopped = 1; }
void closeDisplay() {
    if (menuFont) TTF_CloseFont(menuFont);
    menuFont = nullptr;
    TTF_Quit();
    if (converted) SDL_FreeSurface(converted);
    if (source) SDL_FreeSurface(source);
    converted = source = output = nullptr;
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    texture = nullptr; renderer = nullptr; window = nullptr;
    SDL_Quit();
}
void quitAtFrameBoundary() {
    // Upstream has no dispatch stop API. Memcard writes are synchronous;
    // finish stdio and filesystem writes before exiting at a frame boundary.
    std::fprintf(stderr, "PCSX_FRONTEND_STOP frames=%llu\n",
                 static_cast<unsigned long long>(frames));
    if (benchFrames) {
        uint32_t hash = 2166136261u;
        unsigned colored = 0;
        for (const auto p : pixels) {
            const int r = ((p >> 11) & 31) * 255 / 31, g = ((p >> 5) & 63) * 255 / 63, b = (p & 31) * 255 / 31;
            if (std::abs(r-g) > 12 || std::abs(r-b) > 12) ++colored;
        }
        for (const auto p : pixels) { hash = (hash ^ (p & 255)) * 16777619u; hash = (hash ^ (p >> 8)) * 16777619u; }
        std::fprintf(stderr, "PCSX_BENCH frames=%llu elapsed_ms=%llu video_ms=%llu method=%s hash=%08x\n",
        static_cast<unsigned long long>(frames), static_cast<unsigned long long>(SDL_GetTicks64()-started),
        static_cast<unsigned long long>(videoTime), surfaceVideo ? "surface" : "renderer", hash);
        std::fprintf(stderr, "PCSX_COLOR colored_pixels=%u total=76800 format=RGB565\n", colored);
        const char *capture = std::getenv("LILSMOKE_BENCH_CAPTURE");
        if (capture && !std::strncmp(capture, "/bench/", 7)) {
            FILE *file = std::fopen(capture, "wbx");
            if (file) {
                std::fprintf(file, "P6\n320 240\n255\n");
                for (const auto p : pixels) {
                    const unsigned char rgb[] = {static_cast<unsigned char>(((p >> 11) & 31) * 255 / 31),
                        static_cast<unsigned char>(((p >> 5) & 63) * 255 / 63), static_cast<unsigned char>((p & 31) * 255 / 31)};
                    std::fwrite(rgb, 1, 3, file);
                }
                std::fclose(file);
            }
        }
    }
    std::fflush(nullptr);
    sync();
    closeDisplay();
    std::exit(0);
}
void events() {
    SDL_Event event;
    while (SDL_PollEvent(&event))
        if (event.type == SDL_QUIT ||
            (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F12)) stopped = 1;
    lilsmoke::InputButton button;
    lilsmoke::InputEvent action;
    lilsmoke::PointerEvent pointer;
    while (input.poll(button, action, pointer)) {
        if (action == lilsmoke::InputEvent::Pressed && button == lilsmoke::InputButton::Power)
            stopped = 1;
        if (action == lilsmoke::InputEvent::Pressed && button == lilsmoke::InputButton::Home) {
            paused = !paused; menuChoice = 0; stateMessage.clear();
            continue;
        }
        if (paused && (action == lilsmoke::InputEvent::Pressed || action == lilsmoke::InputEvent::Repeat)) {
            if (button == lilsmoke::InputButton::Up) menuChoice = (menuChoice + 4) % 5;
            if (button == lilsmoke::InputButton::Down) menuChoice = (menuChoice + 1) % 5;
            if (menuChoice == 1 && (button == lilsmoke::InputButton::Left || button == lilsmoke::InputButton::Right)) {
                stateSlot = (stateSlot + (button == lilsmoke::InputButton::Left ? 9 : 1)) % 10;
                stateMessage.clear();
            }
            if (action == lilsmoke::InputEvent::Pressed && button == lilsmoke::InputButton::B)
                paused = false;
            if (action == lilsmoke::InputEvent::Pressed && button == lilsmoke::InputButton::A) {
                if (menuChoice == 4) stopped = 1;
                else if (menuChoice == 0) paused = false;
                else if (menuChoice == 1) { stateSlot = (stateSlot + 1) % 10; stateMessage.clear(); }
                else { stateAction = menuChoice; break; }
            }
        }
    }
    const auto &state = input.state();
    if (state.start && state.select) stopped = 1;
    const bool chord = state.start && state.x;
    if (chord && !menuChord) { paused = !paused; menuChoice = 0; stateMessage.clear(); }
    menuChord = chord;
    if (stopped) quitAtFrameBoundary();
}
void pauseMenu() {
    pcsx_audio_pause(1);
    if (!menuFont && !TTF_Init()) menuFont = TTF_OpenFont("/usr/share/fonts/dejavu/DejaVuSans.ttf", 18);
    SDL_Surface *canvas = output ? output : SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 16, SDL_PIXELFORMAT_RGB565);
    while (paused && !stopped) {
        // Entered via EmuUpdate's cache-safe pad_update boundary. Loading from
        // GPU_updateLace is too late for the core's rcntFreezeLoaded guard.
        if (stateAction) {
            if (stateAction == 2) stateStore.save(stateSlot, stateMessage);
            else stateStore.load(stateSlot, stateMessage);
            stateAction = 0;
        }
        if (canvas) {
            SDL_FillRect(canvas, nullptr, SDL_MapRGB(canvas->format, 24, 28, 32));
            const std::string slot = "State slot: " + std::to_string(stateSlot) + (stateStore.exists(stateSlot) ? " (saved)" : " (empty)");
            const std::string labels[] = {"PCSX4ALL", "Resume", slot, "Save state", "Load state", "Exit game", stateMessage};
            const int spacing = std::min(36, (canvas->h - 12) / 7);
            for (int row = 0; row < 7; ++row) {
                SDL_Color color = row == menuChoice + 1 ? SDL_Color{80, 210, 160, 255} : SDL_Color{240, 240, 240, 255};
                SDL_Surface *label = menuFont ? TTF_RenderUTF8_Blended(menuFont, labels[row].c_str(), color) : nullptr;
                if (label) {
                    SDL_Rect target{12, 8 + row * spacing, std::min(label->w, canvas->w - 24), label->h};
                    if (label->w > target.w) {
                        SDL_Surface *fitted = SDL_CreateRGBSurfaceWithFormat(0, target.w, target.h, 32, SDL_PIXELFORMAT_ARGB8888);
                        if (fitted) { SDL_BlitScaled(label, nullptr, fitted, nullptr); SDL_BlitSurface(fitted, nullptr, canvas, &target); SDL_FreeSurface(fitted); }
                    } else SDL_BlitSurface(label, nullptr, canvas, &target);
                    SDL_FreeSurface(label);
                }
            }
            if (output) SDL_UpdateWindowSurface(window);
            else {
                SDL_UpdateTexture(texture, nullptr, canvas->pixels, canvas->pitch);
                SDL_RenderClear(renderer); SDL_RenderCopy(renderer, texture, nullptr, nullptr); SDL_RenderPresent(renderer);
            }
        }
        SDL_Delay(16);
        events();
    }
    if (canvas && !output) SDL_FreeSurface(canvas);
    if (output) { SDL_FillRect(output, nullptr, 0); SDL_UpdateWindowSurface(window); }
    pcsx_audio_pause(0);
}
uint16_t controller(int port) {
    if (port != 0) return 0xffff;
    events();
    if (paused) { pauseMenu(); return 0xffff; }
    const auto &s = input.state();
    uint16_t state = 0xffff;
    const bool pressed[] = {s.select,s.l3,s.r3,s.start,s.up,s.right,s.down,s.left,
                            s.l2,s.r2,s.l1,s.r1,s.x,s.b,s.a,s.y};
    for (unsigned bit = 0; bit < 16; ++bit) if (pressed[bit]) state &= ~(1U << bit);
    if (benchGameplay) {
        // Frame-based diagnostic input keeps display comparisons deterministic.
        if (frames >= 200 && frames < 215) state &= ~(1U << 3);
        if (frames >= 650 && frames < 665) state &= ~(1U << 14);
        if (frames >= 1000 && frames < 1100) state &= ~(1U << 4);
        if (frames >= 1200 && frames < 1250) state &= ~(1U << 14);
        if (frames >= 1400 && frames < 1450) state &= ~(1U << 13);
    }
    return state;
}
void video(const uint16_t *frame, int width, int height, int pitch) {
    events();
    if (!frame || width != 320 || height != 240 || pitch < width) {
        std::fprintf(stderr, "PCSX: invalid frame geometry\n");
        stopped = 1;
        return;
    }
    const uint64_t begin = benchFrames ? SDL_GetTicks64() : 0;
    if (surfaceVideo) {
        // Convert at PS1 resolution, then enlarge in the destination format.
        // Keep the black letterbox, rather than repainting it every frame.
        source->pixels = const_cast<uint16_t *>(frame); source->pitch = pitch * 2;
        const int copied = nativeVideo ? SDL_BlitSurface(source, nullptr, output, &destination) :
            (SDL_BlitSurface(source, nullptr, converted, nullptr) ||
             SDL_BlitScaled(converted, nullptr, output, &destination));
        if (copied || SDL_UpdateWindowSurfaceRects(window, &destination, 1)) {
            std::fprintf(stderr, "PCSX surface: %s\n", SDL_GetError()); stopped = 1; return;
        }
    } else if (SDL_UpdateTexture(texture, nullptr, frame, pitch * 2) ||
        SDL_RenderClear(renderer) || SDL_RenderCopy(renderer, texture, nullptr, nullptr)) {
        std::fprintf(stderr, "PCSX video: %s\n", SDL_GetError());
        stopped = 1;
        return;
    }
    if (!surfaceVideo) SDL_RenderPresent(renderer);
    if (benchFrames) videoTime += SDL_GetTicks64() - begin;
    ++frames;
    if (benchFrames && frames >= benchFrames) quitAtFrameBoundary();
    const uint64_t now = SDL_GetTicks64();
    if (now - lastReport >= 3000) {
        std::fprintf(stderr, "PCSX_VIDEO frames=%llu presented_fps=%.2f\n",
                     static_cast<unsigned long long>(frames),
                     (frames - reportFrames) * 1000.0 / (now - lastReport));
        lastReport = now; reportFrames = frames;
    }
}
void log(const char *message) { if (message) std::fputs(message, stderr); }
}
int main(int argc, char **argv) {
    if (argc < 1 || argc > 3) {
        std::fprintf(stderr, "usage: pcsx4all-launch DISC [CUE]\n"); return 1;
    }
    std::error_code error;
    std::string selected;
    if (argc == 1) {
        selected = pcsxNativeMenu();
        if (selected.empty()) return 0;
        // Use the same BIOS/config/audio setup as a console-list launch.
        char *args[] = {const_cast<char *>("lilsmoke-launch"), const_cast<char *>("psx"),
                        const_cast<char *>(selected.c_str()), const_cast<char *>("--pcsx4all"), nullptr};
        std::fprintf(stderr, "PCSX_OWN_MENU_GAME_HANDOFF\n");
        const char *hardware = std::getenv("LILSMOKE_HCFB");
        const char *loading = "/usr/lib/lilsmoke/m8-loading-run";
        if (hardware && !std::strcmp(hardware, "1") && access(loading, X_OK) == 0) {
            char *wrapped[] = {const_cast<char *>("m8-loading-run"),
                const_cast<char *>("/usr/lib/lilsmoke/lilsmoke-launch"), args[1], args[2], args[3], nullptr};
            std::fprintf(stderr, "PCSX_OWN_MENU_LOADING_WRAPPER\n");
            execv(loading, wrapped);
            std::perror("PCSX loading wrapper handoff");
        }
        execv("/usr/lib/lilsmoke/lilsmoke-launch", args);
        std::perror("PCSX native game handoff");
        return 127;
    }
    const char *disc = argc == 1 ? selected.c_str() : argc == 3 ? argv[2] : argv[1];
    if (!std::filesystem::is_regular_file(disc, error)) return 3;
    stateStore.init(disc);
    setenv("SDL_VIDEODRIVER", "directfb", 0);
    setenv("SDL_AUDIODRIVER", "alsa", 0);
    setenv("SDL_RENDER_DRIVER", "software", 1);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "PCSX SDL: %s\n", SDL_GetError()); return 10;
    }
    const bool windowed = std::getenv("LILSMOKE_UI_WINDOWED");
    const char *mode = std::getenv("LILSMOKE_PSX_VIDEO");
    surfaceVideo = !mode || !std::strcmp(mode, "surface");
    const char *native = std::getenv("LILSMOKE_M8_NATIVE_VIDEO");
    nativeVideo = native && !std::strcmp(native, "1");
    if (nativeVideo) surfaceVideo = true;
    if (const char *limit = std::getenv("LILSMOKE_BENCH_FRAMES")) {
        char *end = nullptr; benchFrames = std::strtoull(limit, &end, 10);
        if (!end || *end || benchFrames > 100000) return 2;
    }
    benchGameplay = benchFrames && std::getenv("LILSMOKE_BENCH_GAMEPLAY");
    window = SDL_CreateWindow("LilSmokeOS PlayStation", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, nativeVideo ? 320 : 640, nativeVideo ? 240 : 480,
        windowed || nativeVideo ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (window && surfaceVideo) {
        output = SDL_GetWindowSurface(window);
        source = SDL_CreateRGBSurfaceWithFormatFrom(pixels, 320, 240, 16, 640, SDL_PIXELFORMAT_RGB565);
        if (output) {
            if (!nativeVideo)
                converted = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, output->format->BitsPerPixel, output->format->format);
            const int scale = output->w / 320 < output->h / 240 ? output->w / 320 : output->h / 240;
            const int w = scale > 0 ? 320 * scale :
                (output->w * 3 <= output->h * 4 ? output->w : output->h * 4 / 3);
            const int h = scale > 0 ? 240 * scale : w * 3 / 4;
            destination = {(output->w-w)/2, (output->h-h)/2, w, h};
            if (nativeVideo && (output->w != 320 || output->h != 240 || output->format->format != SDL_PIXELFORMAT_RGB565)) {
                std::fprintf(stderr, "PCSX native RGB565 surface unavailable; refusing desktop fallback\n");
                closeDisplay(); return 10;
            }
            SDL_FillRect(output, nullptr, 0); SDL_UpdateWindowSurface(window);
        }
    } else if (window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer) {
        SDL_RenderSetLogicalSize(renderer, 320, 240);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, 320, 240);
    }
    if (surfaceVideo ? (!source || !output || (!nativeVideo && !converted)) : !texture) {
        std::fprintf(stderr, "PCSX display: %s\n", SDL_GetError());
        closeDisplay(); return 10;
    }
    SDL_ShowCursor(SDL_DISABLE);
    input.setKeyboardNavigation(false);
    input.init(640, 480);
    std::signal(SIGTERM, signalStop); std::signal(SIGINT, signalStop);
    rkgame_pcsx_set_work_path("/persist/");
    rkgame_pcsx_set_video_buffer(pixels, 320, 240, 320);
    rkgame_pcsx_set_video_callback(video);
    rkgame_pcsx_set_input_callback(controller);
    rkgame_pcsx_set_log_callback(log);
    lastReport = SDL_GetTicks64();
    started = lastReport;
    std::fprintf(stderr, "PCSX_FRONTEND_READY video=%s input=evdev audio=SDL2/native cursor=disabled\n",
                 nativeVideo ? "native-RGB565-320x240" : "SDL2/DirectFB");
    // PCSX_Load starts and joins a worker. This entry keeps SDL on the main
    // thread and avoids the old launcher's second execution of the same game.
    int result = pcsx_load_game(&disc);
    closeDisplay();
    return result == 0 ? 0 : 4;
}
