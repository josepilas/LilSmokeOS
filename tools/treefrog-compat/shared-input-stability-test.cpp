// SPDX-License-Identifier: MPL-2.0
#include "Input.h"
#include <SDL.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>

namespace lilsmoke {
struct InputTestAccess {
    static void poll(Input &input) { input.pollUsbGamepads(); }
    static void recheck(Input &input) { input.usbReader_.checked=0; }
};
}
int main() {
    char path[]="/tmp/lilsmoke-shared-stability-XXXXXX";
    const int fd=mkstemp(path);assert(fd>=0);assert(!fchmod(fd,0600));
    assert(!ftruncate(fd,sizeof(ls_gp_state)));
    auto *state=static_cast<ls_gp_state*>(mmap(nullptr,sizeof(ls_gp_state),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));assert(state!=MAP_FAILED);
    setenv("LILSMOKE_GAMEPAD_STATE",path,1);
    assert(!SDL_Init(SDL_INIT_EVENTS));
    {
        lilsmoke::Input input;
        const auto publish=[&](uint32_t buttons) {
            __atomic_store_n(&state->sequence,1,__ATOMIC_RELEASE);
            state->magic=LS_GP_MAGIC;state->abi=1;state->pid=getpid();state->millis=ls_gp_now();
            state->pads[2].connected=1;state->pads[2].instance=42;state->pads[2].buttons=buttons;
            state->pads[2].axes[2]=32767;
            __atomic_store_n(&state->sequence,2,__ATOMIC_RELEASE);
        };
        publish(1u<<LS_GP_A);lilsmoke::InputTestAccess::poll(input);
        assert(input.gameplayState().a);assert(input.analogState().rightX>30000);
        __atomic_store_n(&state->sequence,3,__ATOMIC_RELEASE);
        for(unsigned i=0;i<8;i++){lilsmoke::InputTestAccess::poll(input);assert(input.gameplayState().a);usleep(1000);}
        publish(0);lilsmoke::InputTestAccess::poll(input);assert(!input.gameplayState().a);
        publish(1u<<LS_GP_A);lilsmoke::InputTestAccess::poll(input);assert(input.gameplayState().a);
        __atomic_store_n(&state->sequence,3,__ATOMIC_RELEASE);usleep(120000);
        lilsmoke::InputTestAccess::poll(input);assert(!input.gameplayState().a);
        publish(1u<<LS_GP_A);lilsmoke::InputTestAccess::poll(input);assert(input.gameplayState().a);
        __atomic_store_n(&state->sequence,1,__ATOMIC_RELEASE);state->pads[2].connected=0;
        __atomic_store_n(&state->sequence,2,__ATOMIC_RELEASE);
        lilsmoke::InputTestAccess::poll(input);assert(!input.gameplayState().a);
        publish(1u<<LS_GP_A);lilsmoke::InputTestAccess::poll(input);assert(input.gameplayState().a);
        assert(!unlink(path));lilsmoke::InputTestAccess::recheck(input);
        lilsmoke::InputTestAccess::poll(input);assert(!input.gameplayState().a);
    }
    SDL_Quit();munmap(state,sizeof(ls_gp_state));close(fd);
    puts("SHARED_INPUT_STABILITY contention/release/timeout/unplug/removal passed");
}
