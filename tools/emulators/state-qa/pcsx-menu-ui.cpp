// Drive the production frontend's pause loop; synthetic input/video, real core.
#define main pcsxProductionMain
#include "../../../package/pcsx4all/src/pcsx4all-launch.cpp"
#undef main
#include "r3000a.h"
#include "plugins.h"
#include "plugin_lib.h"
#include "spu/spu_pcsxrearmed/spu_config.h"
#include <cassert>
#include <functional>
#include <sys/wait.h>

namespace lilsmoke {
struct InputTestAccess {
    static void button(Input &value, InputButton button) {
        value.queueButton(button, InputEvent::Pressed);
        value.queueButton(button, InputEvent::Released);
    }
    static void chord(Input &value, bool down) {
        value.state_.start = down; value.state_.x = down;
    }
};
}

namespace {
using Button = lilsmoke::InputButton;
std::vector<std::function<void()>> steps;
size_t step = 0;
bool driving = false, helperPresent = false, exitChild = false;
std::string mode;
std::vector<std::vector<std::string>> execCalls;
std::vector<int> audioPauses;
const char *logicalROM = "/roms/O'Brien $(not-a-command); Game.zip";

void press(Button button) { lilsmoke::InputTestAccess::button(input, button); }
void snapshot(const char *name) {
    const std::string path = "/bench/pcsx-ui-" + mode + "-" + name + ".bmp";
    SDL_Surface *capture = output ? SDL_ConvertSurfaceFormat(output, SDL_PIXELFORMAT_RGB24, 0) :
        SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 24, SDL_PIXELFORMAT_RGB24);
    assert(capture);
    if (!output) assert(SDL_RenderReadPixels(renderer, nullptr, capture->format->format, capture->pixels, capture->pitch) == 0);
    size_t varied = 0;
    auto *bytes = static_cast<unsigned char *>(capture->pixels);
    for (int y = 0; y < capture->h; ++y)
        for (int x = 0; x < capture->w; ++x) {
            const auto *p = bytes + y * capture->pitch + x * 3;
            if (p[0] > 60 || p[1] > 60 || p[2] > 60) ++varied;
        }
    assert(varied > 1000);
    assert(SDL_SaveBMP(capture, path.c_str()) == 0);
    SDL_FreeSurface(capture);
}

void frameWithMenu(std::initializer_list<std::function<void()>> actions, bool chord = false) {
    steps = actions; step = 0; driving = true;
    if (chord) lilsmoke::InputTestAccess::chord(input, true);
    else press(Button::Home);
    video(pixels, 320, 240, 320);
    driving = false;
    assert(step == steps.size() && !paused && !stopped);
    assert(audioPauses.size() >= 2 && audioPauses[audioPauses.size() - 2] == 1 && audioPauses.back() == 0);
}
}

std::string pcsxNativeMenu() { return logicalROM; }
extern "C" int __real_access(const char *, int);
extern "C" int __wrap_access(const char *path, int mode) {
    if (!std::strcmp(path, "/usr/lib/lilsmoke/m8-loading-run") && mode == X_OK) {
        if (helperPresent) return 0;
        errno = ENOENT; return -1;
    }
    return __real_access(path, mode);
}
extern "C" int __wrap_execv(const char *path, char *const argv[]) {
    std::vector<std::string> call{path};
    for (size_t i = 0; argv[i]; ++i) call.emplace_back(argv[i]);
    execCalls.push_back(call);
    errno = ENOENT; return -1;
}
extern "C" void __real_SDL_Delay(Uint32);
extern "C" void __wrap_SDL_Delay(Uint32 delay) {
    if (!driving) { __real_SDL_Delay(delay); return; }
    assert(step < steps.size());
    steps[step++]();
}
extern "C" void __real_pcsx_audio_pause(int);
extern "C" void __wrap_pcsx_audio_pause(int pause) {
    audioPauses.push_back(pause);
    __real_pcsx_audio_pause(pause);
}
extern "C" void __real_SDL_Quit(void);
extern "C" void __wrap_SDL_Quit(void) {
    // DirectFB has worker threads. Do not attempt their shutdown in a forked
    // UI-test child; this checks the exit action, not the platform teardown.
    if (exitChild) {
        assert(stopped && menuChoice == 4);
        std::fprintf(stderr, "PCSX_UI_EXIT_ACTION_PASS postfork_driver_teardown_skipped=1\n");
        _exit(0);
    }
    __real_SDL_Quit();
}

int main(int argc, char **argv) {
    assert(argc == 2); mode = argv[1];
    assert(mode == "surface" || mode == "renderer");
    char *launch[] = {const_cast<char *>("pcsx4all"), nullptr};
    for (const auto &test : std::vector<std::pair<const char *, bool>>{{"1", true}, {"1", false}, {"0", true}}) {
        setenv("LILSMOKE_HCFB", test.first, 1); helperPresent = test.second; execCalls.clear();
        assert(pcsxProductionMain(1, launch) == 127);
        const bool wrapped = test.second && !std::strcmp(test.first, "1");
        assert(execCalls.size() == (wrapped ? 2U : 1U));
        if (wrapped) assert((execCalls[0] == std::vector<std::string>{
            "/usr/lib/lilsmoke/m8-loading-run", "m8-loading-run", "/usr/lib/lilsmoke/lilsmoke-launch", "psx", logicalROM, "--pcsx4all"}));
        assert((execCalls.back() == std::vector<std::string>{
            "/usr/lib/lilsmoke/lilsmoke-launch", "lilsmoke-launch", "psx", logicalROM, "--pcsx4all"}));
    }
    std::puts("PCSX_OWN_MENU_EXEC_ARGV_PASS m8_wrapper=1 generic_direct=1 missing_helper_fallback=1 failed_exec_fallback=1 literal_rom=1");
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    window = SDL_CreateWindow("PCSX UI contract", 0, 0, 320, 240, 0); assert(window);
    surfaceVideo = mode == "surface"; nativeVideo = true;
    destination = {0, 0, 320, 240};
    if (surfaceVideo) {
        output = SDL_GetWindowSurface(window); assert(output);
        source = SDL_CreateRGBSurfaceWithFormatFrom(pixels, 320, 240, 16, 640, SDL_PIXELFORMAT_RGB565); assert(source);
    } else {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE); assert(renderer);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, 320, 240); assert(texture);
    }
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x) pixels[y * 320 + x] = ((x / 32 + y / 32) % 2) ? 0xf81f : 0x07e0;
    const std::string root = "/bench/ui-states-" + mode;
    std::filesystem::create_directories(root);
    setenv("LILSMOKE_STATE_ROOT", root.c_str(), 1); unsetenv("LILSMOKE_STATE_CONTENT");
    stateStore.init("/roms/UI contract.cue");
    std::error_code ec; std::filesystem::remove(stateStore.path(2), ec);
    memset(&Config, 0, sizeof Config);
    Config.Cpu = CPU_INTERPRETER; Config.HLE = 1; Config.PsxAuto = 1;
    Config.SpuUpdateFreq = SPU_UPDATE_FREQ_16;
    snprintf(Config.Mcd1, sizeof Config.Mcd1, "%s/card1.mcr", root.c_str());
    snprintf(Config.Mcd2, sizeof Config.Mcd2, "%s/card2.mcr", root.c_str());
    spu_config.iHaveConfiguration = 1; spu_config.iVolume = 1024;
    spu_config.iUseFixedUpdates = 1; spu_config.iTempo = 1;
    rkgame_pcsx_set_video_buffer(pixels, 320, 240, 320);
    assert(psxInit() == 0 && LoadPlugins() == 0); pl_init(); psxReset();
    psxM[0x1200] = 0x42; psxRegs.GPR.r[8] = 0x12345678;
    frameWithMenu({
        [] { assert(paused && menuChoice == 0); assert(menuFont); snapshot("01-menu"); press(Button::Down); },
        [] { assert(menuChoice == 1); press(Button::Left); },
        [] { assert(stateSlot == 9); press(Button::Right); },
        [] { assert(stateSlot == 0); press(Button::A); },
        [] { assert(stateSlot == 1); press(Button::Down); },
        [] { assert(menuChoice == 2); press(Button::A); },
        [] { assert(stateMessage == "Saved slot 1" && stateStore.exists(1)); snapshot("02-saved");
             psxM[0x1200] = 0x65; psxRegs.GPR.r[8] = 7; press(Button::Down); },
        [] { assert(menuChoice == 3); press(Button::A); },
        [] { assert(stateMessage == "Loaded slot 1" && psxM[0x1200] == 0x42 && psxRegs.GPR.r[8] == 0x12345678);
             snapshot("03-loaded"); press(Button::Up); },
        [] { assert(menuChoice == 2); press(Button::Up); },
        [] { assert(menuChoice == 1); press(Button::Right); },
        [] { assert(stateSlot == 2); press(Button::Down); },
        [] { press(Button::Down); },
        [] { assert(menuChoice == 3); press(Button::A); },
        [] { assert(paused && stateMessage == "This state slot is empty"); snapshot("04-empty-error"); press(Button::B); },
    });
    snapshot("05-resumed");
    frameWithMenu({
        [] { assert(paused && menuChoice == 0); lilsmoke::InputTestAccess::chord(input, false); press(Button::Down); },
        [] { press(Button::Left); },
        [] { assert(stateSlot == 1); std::filesystem::resize_file(stateStore.path(1), 64); press(Button::Down); },
        [] { press(Button::Down); },
        [] { press(Button::A); },
        [] { assert(paused && stateMessage == "Invalid or damaged saved state" && psxM[0x1200] == 0x42);
             snapshot("06-corrupt-error"); press(Button::B); },
    }, true);
    setenv("LILSMOKE_STATE_ROOT", "/proc/no-ui-state-writes", 1); stateStore.init("/roms/UI contract.cue");
    frameWithMenu({
        [] { press(Button::Down); },
        [] { press(Button::Down); },
        [] { press(Button::A); },
        [] { assert(paused && stateMessage == "Cannot create state directory"); snapshot("07-storage-error"); press(Button::B); },
    });
    frameWithMenu({[] { assert(menuChoice == 0); press(Button::A); }});
    const pid_t child = fork(); assert(child >= 0);
    if (!child) {
        exitChild = true;
        frameWithMenu({[] { press(Button::Up); }, [] { assert(menuChoice == 4); press(Button::A); }});
        _exit(88);
    }
    int status = 0; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    std::printf("PCSX_PAUSE_UI_PASS mode=%s home=1 chord=1 slot_wrap=1 save_button=1 load_button=1 real_ram_restored=1 missing_error=1 corrupt_error=1 storage_error=1 resume_b=1 resume_a=1 exit_action=1 driver_teardown_tested=0 audio_pause_resume=1\n", mode.c_str());
    std::fflush(nullptr);
    _exit(0);
}
