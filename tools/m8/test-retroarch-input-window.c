#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include "../../package/retroarch-mips/m8_sdl2_window.h"
int main(void) {
    unsigned width = 856, height = 480, flags = 0x1001;
    unsetenv("LILSMOKE_HCFB");
    assert(!m8_sdl2_window(&width, &height, &flags));
    assert(width == 856 && height == 480 && flags == 0x1001);
    setenv("LILSMOKE_HCFB", "yes", 1);
    assert(!m8_sdl2_window(&width, &height, &flags));
    setenv("LILSMOKE_HCFB", "1", 1);
    assert(m8_sdl2_window(&width, &height, &flags));
    assert(width == 1280 && height == 720 && flags == 1);
    setenv("LILSMOKE_SCREEN_WIDTH", "854", 1); setenv("LILSMOKE_SCREEN_HEIGHT", "480", 1);
    assert(m8_sdl2_window(&width, &height, &flags) && width == 854 && height == 480 && flags == 1);
    setenv("LILSMOKE_SCREEN_WIDTH", "99999999", 1);
    assert(m8_sdl2_window(&width, &height, &flags) && width == 1280 && height == 720 && flags == 1);
    puts("PASS: gated M8 exclusive fullscreen mode, validated QA dimensions, no stock desktop, GL, GPU or hardware accesses.");
    return 0;
}
