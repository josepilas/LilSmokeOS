// Actual title + production menu/controller/video paths. Inputs stay in the guest.
#define main pcsxProductionMain
#include "../../../package/pcsx4all/src/pcsx4all-launch.cpp"
#undef main
#include "r3000a.h"
#include "misc.h"
#include <cassert>
#include <vector>
#include <cstddef>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>

namespace lilsmoke {
struct InputTestAccess {
    static void press(InputButton button) {
        input.queueButton(button, InputEvent::Pressed);
        input.queueButton(button, InputEvent::Released);
    }
};
}

namespace {
unsigned updates, menuStep, testPresents;
std::vector<char> checkpoint;
uint32_t checkpointPc, checkpointReg;
std::string testMode;
using Button = lilsmoke::InputButton;

void deny(int number) {
    sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<unsigned>(number), 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
    };
    sock_fprog filter = {4, code};
    assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
    assert(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter) == 0);
}

uint16_t testController(int port) {
    if (!port) {
        ++updates;
        if (updates == 90 || updates == 180) {
            assert(Config.Cpu == CPU_DYNAREC && reinterpret_cast<uintptr_t>(psxM) == 0);
            menuStep = 0;
            lilsmoke::InputTestAccess::press(Button::Home);
        }
        if (updates == 240) {
            assert(!paused && !stopped && testPresents > 0);
            assert(std::memcmp(checkpoint.data(), psxM, checkpoint.size()) != 0);
            std::fprintf(stderr, "PCSX_RECOVERY_MENU_PASS mode=%s updates=%u presented=%u ram=restored cpu=restored resumed=1 disc=%s\n",
                         testMode.c_str(), updates, testPresents, CdromId);
            std::fflush(nullptr);
            _exit(0);
        }
    }
    return controller(port);
}
void testVideo(const uint16_t *frame, int w, int h, int pitch) {
    ++testPresents;
    video(frame, w, h, pitch);
}
}

std::string pcsxNativeMenu() { return {}; }
extern "C" void __real_SDL_Delay(Uint32);
extern "C" void __wrap_SDL_Delay(Uint32 delay) {
    if (!paused) { __real_SDL_Delay(delay); return; }
    assert(updates == 90 || updates == 180);
    const unsigned actionRow = updates == 90 ? 2 : 3;
    if (menuStep < actionRow) lilsmoke::InputTestAccess::press(Button::Down);
    else if (menuStep == actionRow) lilsmoke::InputTestAccess::press(Button::A);
    else {
        if (updates == 90) {
            assert(stateMessage == "Saved slot 0" && stateStore.exists(0));
            checkpoint.assign(psxM, psxM + 0x200000);
            checkpointPc = psxRegs.pc; checkpointReg = psxRegs.GPR.r[8];
        } else {
            assert(stateMessage == "Loaded slot 0");
            assert(std::memcmp(checkpoint.data(), psxM, checkpoint.size()) == 0);
            assert(psxRegs.pc == checkpointPc && psxRegs.GPR.r[8] == checkpointReg);
        }
        if (output) assert(SDL_SaveBMP(output, ("/bench/menu-" + testMode + (updates == 90 ? "-saved.bmp" : "-loaded.bmp")).c_str()) == 0);
        lilsmoke::InputTestAccess::press(Button::B);
    }
    ++menuStep;
    assert(menuStep <= 5);
}
extern "C" int __real_pcsx_load_game(const char **);
extern "C" int __wrap_pcsx_load_game(const char **disc) {
    rkgame_pcsx_set_input_callback(testController);
    rkgame_pcsx_set_video_callback(testVideo);
    return __real_pcsx_load_game(disc);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    testMode = argv[2];
    assert(testMode == "surface" || testMode == "renderer");
    rlimit limit = {80 * 1024 * 1024, 80 * 1024 * 1024};
    assert(setrlimit(RLIMIT_DATA, &limit) == 0);
    setenv("LILSMOKE_PSX_VIDEO", testMode.c_str(), 1);
    setenv("LILSMOKE_STATE_ROOT", ("/bench/menu-states-" + testMode).c_str(), 1);
    setenv("LILSMOKE_UI_WINDOWED", "1", 1);
    deny(SYS_socketpair);
    deny(SYS_fsync);
    char *args[] = {argv[0], argv[1], nullptr};
    return pcsxProductionMain(2, args);
}
