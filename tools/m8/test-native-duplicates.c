#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../package/retroarch-mips/m8_native_frame.h"
static unsigned presents, menus, resets, checkpoints;
static uint16_t expected;
static int present(const void *p,unsigned w,unsigned h,unsigned pitch,int rgb32,unsigned n,unsigned d) {
    assert(w==2&&h==2&&pitch>=4&&!rgb32&&n==13333&&d==10000);
    assert(((const uint16_t *)p)[0]==expected);
    ++presents;return 1;
}
static int menu(const void *p,unsigned w,unsigned h,unsigned pitch,int rgb32) {
    assert(p&&w==2&&h==2&&pitch==4&&!rgb32);++menus;return 1;
}
static void reset(void){++resets;}
static void checkpoint(void){++checkpoints;}
int main(void) {
    uint16_t pixels[6]={0xf800,0x07e0,0xaaaa,0x001f,0xffff,0xbbbb};
    expected=pixels[0];
    m8_presentation.present=present;m8_presentation.menu=menu;
    m8_presentation.reset=reset;m8_presentation.checkpoint=checkpoint;m8_presentation.looked_up=1;
    unsetenv("LILSMOKE_HCFB");
    assert(!m8_native_frame(pixels,2,2,6,0,1.3333f,0,0));
    setenv("LILSMOKE_HCFB","1",1);
    assert(!m8_native_frame(NULL,0,0,0,0,1.3333f,0,0));
    assert(m8_native_frame(pixels,2,2,6,0,1.3333f,0,0));
    assert(presents==1&&resets==0&&m8_presentation.pitch==4&&m8_presentation.capacity==8);
    for(unsigned i=0;i<600;++i)assert(m8_native_frame(NULL,0,0,0,0,1.3333f,0,0));
    assert(presents==1&&resets==0);
    pixels[0]=0; // The borrowed libretro frame is mutable/reused.
    assert(m8_native_menu_frame(pixels,2,2,4,0));
    assert(menus==1&&checkpoints==1&&m8_presentation.in_menu);
    assert(m8_native_frame(NULL,0,0,0,0,1.3333f,0,0));
    assert(presents==2&&!m8_presentation.in_menu&&resets==0);
    assert(!m8_native_frame(pixels,641,2,6,0,1.3333f,0,0));
    assert(!m8_native_frame(pixels,2,2,6,0,NAN,0,0));
    assert(!m8_native_frame(pixels,2,2,6,0,1.3333f,0,1));
    assert(resets==1);
    assert(m8_native_frame(NULL,0,0,0,0,1.3333f,0,0));
    assert(presents==3);
    m8_native_cache_release();
    assert(resets==2&&!m8_presentation.game&&!m8_presentation.cached);
    assert(!m8_native_frame(NULL,0,0,0,0,1.3333f,0,0));
    puts("PASS: 600 duplicate frames cause zero resets/copies; owned RGB565 frame restores after menu; bounded geometry, NaN, rotation, teardown");
}
