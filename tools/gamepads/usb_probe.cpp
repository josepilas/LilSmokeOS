#include <SDL.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include "../../package/lilsmoke-sm64-original/src/input_bridge.cpp"
int main(int argc,char **argv) {
    const bool live=argc==2 && !std::strcmp(argv[1],"--live");
    SDL_Init(SDL_INIT_TIMER);
    sm64_native_input_init();
    bool seen = false, released = false;
    bool attack=false,crouch=false,camera=false,movement=false;
    for (unsigned i = 0; i < (live ? 60000u : 500u); ++i) {
        sm64_native_input_poll();
        uint16_t buttons = 0; int8_t x = 0, y = 0;
        sm64_native_input_read(&buttons, &x, &y);
        attack |= (buttons & 0x4000)!=0; crouch |= (buttons & 0x2000)!=0;
        camera |= (buttons & 0x0008)!=0; movement |= x>20;
        if (i % 50 == 0) { std::printf("Probe frame=%u buttons=%04x stick=%d,%d observed=%d%d%d%d\n", i, buttons, x, y,attack,crouch,camera,movement);std::fflush(stdout); }
        if(live) seen=attack && crouch && camera && movement;
        else if ((buttons & 0x6008) == 0x6008 && x > 20 && x < 50) seen = true;
        if (seen && buttons == 0 && x == 0 && y == 0) released = true;
        if(live && released)break;
        usleep(10000);
    }
    sm64_native_input_close(); SDL_Quit();
    std::printf("USB_PAD_PROBE seen=%d released=%d\n", seen, released);
    return seen && released ? 0 : 1;
}
